#include "preview.h"

#include <algorithm>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
    void Require(bool value, const char* message)
    {
        if (!value) throw std::runtime_error(message);
    }

    std::vector<unsigned char> Bytes(const std::string& text)
    {
        return std::vector<unsigned char>(text.begin(), text.end());
    }
}

int main()
{
    int groups = 0;
    int failures = 0;
    auto Run = [&](const char* name, const std::function<void()>& test) {
        ++groups;
        try { test(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& e) { ++failures; std::cerr << "FAIL " << name << ": " << e.what() << '\n'; }
    };
    Run("empty input", [] {
        for (auto format : {preview::Format::Hex, preview::Format::Text})
        {
            const auto page = preview::Render({}, 999, format);
            Require(page.count == 0 && page.length == 0 && page.text.empty(), "empty render failed");
        }
    });
    Run("exact hex offsets and non-printable bytes", [] {
        const auto page = preview::Render({0, 'A', 0xff}, 0, preview::Format::Hex);
        Require(page.text.find("00000000  00 41 FF ") == 0, "hex prefix incorrect");
        Require(page.text.find("|.A.|\n") != std::string::npos, "ASCII column incorrect");
        Require(page.text.find('\0') == std::string::npos, "NUL in display");
        Require(page.length == 3 && page.count == 1, "metadata incorrect");
    });
    Run("hex text positions select bytes across rows and groups", [] {
        std::vector<unsigned char> bytes(preview::PageBytes, 0);
        for (size_t byte : {size_t(0), size_t(7), size_t(8), size_t(16), bytes.size() - 1})
            bytes[byte] = 0xab;
        const auto page = preview::Render(bytes, 0, preview::Format::Hex);
        for (size_t byte : {size_t(0), size_t(7), size_t(8), size_t(16), bytes.size() - 1})
            Require(page.text.substr(preview::HexByteTextPosition(byte), 2) == "AB", "wrong hex text position");
    });
    Run("bounded pages and clamped last page", [] {
        std::vector<unsigned char> bytes(preview::PageBytes * 2 + 7, 'a');
        const auto original = bytes;
        const auto first = preview::Render(bytes, 0, preview::Format::Hex);
        const auto last = preview::Render(bytes, preview::NotFound, preview::Format::Hex);
        Require(first.length == preview::PageBytes && first.text.size() < 25000, "unbounded output");
        Require(last.index == 2 && last.offset == 8192 && last.length == 7, "page clamping failed");
        Require(last.text.find("00002000") == 0, "last page offset incorrect");
        Require(bytes == original, "render modified input");
        Require(preview::Render(std::vector<unsigned char>(4096), 0, preview::Format::Hex).count == 1,
                "exact multiple added a page");
    });
    Run("UTF-8 text preserves Chinese and sanitizes binary", [] {
        const std::string chinese = "\xe9\x85\x8d\xe7\xbd\xae";
        auto input = Bytes(chinese + "\n\tA");
        input.insert(input.end(), {0, 0xff, 0x1b, 0x7f});
        const auto page = preview::Render(input, 0, preview::Format::Text);
        Require(page.text == chinese + "\n\tA....", "UTF-8 display mismatch");
    });
    Run("invalid UTF-8 and page-boundary sequences are safe", [] {
        const std::vector<unsigned char> invalid = {0xc0,0xaf,0xed,0xa0,0x80,0xf4,0x90,0x80,0x80,0xe2};
        Require(preview::Render(invalid, 0, preview::Format::Text).text == std::string(invalid.size(), '.'),
                "invalid sequence leaked");
        std::vector<unsigned char> bytes(4095, 'a');
        bytes.insert(bytes.end(), {0xe9,0x85,0x8d});
        Require(preview::Render(bytes, 0, preview::Format::Text).text.back() == '.', "boundary prefix leaked");
        Require(preview::Render(bytes, 1, preview::Format::Text).text == "..", "boundary suffix leaked");
    });
    Run("readable runs carry source offsets and minimum length", [] {
        const std::vector<unsigned char> bytes = {0,'a','b','c',0,'n','a','m','e','=','x',0,'t','a','i','l'};
        const auto strings = preview::IndexStrings(bytes);
        Require(strings.size() == 2, "readable string count incorrect");
        Require(strings[0].offset == 5 && strings[0].length == 6, "first readable range incorrect");
        Require(strings[1].offset == 12 && strings[1].length == 4, "trailing readable range incorrect");
        Require(preview::IndexStrings(std::vector<unsigned char>(4096, 0)).empty(), "zero-filled file produced strings");
    });
    Run("string index keeps UTF-8 runs across former page boundaries", [] {
        std::vector<unsigned char> bytes(preview::PageBytes * 3 + 20, 0);
        const std::string phrase = "\xe9\x85\x8d\xe7\xbd\xae\xe5\x88\x97\xe8\xa1\xa8";
        const size_t boundary = preview::PageBytes - 1;
        std::copy(phrase.begin(), phrase.end(), bytes.begin() + boundary);
        std::copy(phrase.begin(), phrase.end(), bytes.end() - phrase.size());
        const auto strings = preview::IndexStrings(bytes);
        Require(strings.size() == 2 && strings[0].offset == boundary && strings[0].length == phrase.size(),
                "cross-page UTF-8 run split or size incorrect");
        auto match = preview::FindString(bytes, strings, phrase, 0);
        Require(match.row == 0 && match.offset == boundary, "first string match missing");
        match = preview::FindString(bytes, strings, phrase, boundary + 1);
        Require(match.row == 1 && match.offset == bytes.size() - phrase.size(), "later string match missing");
        Require(preview::FindString(bytes, strings, std::string("\0", 1) + phrase, 0).offset == preview::NotFound,
                "match crossed the start of a string");
        Require(preview::FindString(bytes, strings, phrase, bytes.size()).offset == preview::NotFound,
                "search at EOF returned a match");
    });
    Run("full-file search crosses pages and accepts UTF-8", [] {
        std::vector<unsigned char> bytes(9000, 0);
        const auto needle = Bytes("setting");
        std::copy(needle.begin(), needle.end(), bytes.begin() + 4094);
        std::copy(needle.begin(), needle.end(), bytes.begin() + 8000);
        Require(preview::Find(bytes, "setting", 0) == 4094, "cross-page match missing");
        Require(preview::Find(bytes, "setting", 4095) == 8000, "next match missing");
        Require(preview::Find(bytes, "SETTING", 0) == preview::NotFound, "search lost case sensitivity");
        const std::string chinese = "\xe9\x85\x8d\xe7\xbd\xae";
        Require(preview::Find(Bytes("prefix" + chinese), chinese, 0) == 6, "UTF-8 search mismatch");
        const std::vector<unsigned char> binary{0xff, 0, 0xff, 0};
        const std::string pattern("\xff\0", 2);
        Require(preview::Find(binary, pattern, 0) == 0 && preview::Find(binary, pattern, 1) == 2,
                "binary search lost high bytes, NULs or starting offset");
        Require(preview::Find(binary, pattern, 3) == preview::NotFound, "partial binary match accepted");
        Require(preview::Find(bytes, "", 0) == preview::NotFound, "empty query accepted");
        Require(preview::Find(bytes, "setting", bytes.size()) == preview::NotFound, "out-of-range search accepted");
    });
    Run("all byte values remain bounded in every format", [] {
        std::vector<unsigned char> bytes(256);
        for (size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<unsigned char>(i);
        for (auto format : {preview::Format::Hex, preview::Format::Text})
        {
            const auto page = preview::Render(bytes, 0, format);
            Require(page.text.find('\0') == std::string::npos && page.length == 256, "unsafe output");
        }
    });
    std::cout << groups - failures << '/' << groups << " preview test groups passed\n";
    return failures == 0 ? 0 : 1;
}
