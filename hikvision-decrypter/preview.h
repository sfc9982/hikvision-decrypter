#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace preview
{
    constexpr size_t PageBytes = 4096;
    constexpr size_t NotFound = static_cast<size_t>(-1);
    enum class Format { Hex, Text };

    struct Page
    {
        std::string text;
        size_t index = 0;
        size_t count = 0;
        size_t offset = 0;
        size_t length = 0;
    };

    // Compact index entries for files up to 4 GiB minus one byte. No string copies.
    struct StringRange
    {
        std::uint32_t offset;
        std::uint32_t length;
        size_t End() const { return static_cast<size_t>(offset) + length; }
    };

    struct StringMatch
    {
        size_t row = NotFound;
        size_t offset = NotFound;
    };

    std::vector<StringRange> IndexStrings(const std::vector<unsigned char>& bytes);
    // The index must have been built from these same bytes and be in source order.
    StringMatch FindString(const std::vector<unsigned char>& bytes, const std::vector<StringRange>& index,
                           const std::string& query, size_t start);
    size_t HexByteTextPosition(size_t byteInPage);
    Page Render(const std::vector<unsigned char>& bytes, size_t page, Format format);
    size_t Find(const std::vector<unsigned char>& bytes, const std::string& query, size_t start);
}
