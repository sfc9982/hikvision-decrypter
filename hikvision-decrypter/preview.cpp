#include "preview.h"

#include <algorithm>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace
{
    constexpr size_t HexRowBytes = 16;
    constexpr size_t HexGroupBytes = 8;
    constexpr size_t HexOffsetWidth = 10; // Eight hex digits and two spaces.
    constexpr size_t HexByteWidth = 3;
    constexpr size_t HexRowWidth = HexOffsetWidth + HexRowBytes * HexByteWidth
        + 1 + 2 + HexRowBytes + 2; // Group separator, ASCII borders and newline.
    size_t Utf8Width(const unsigned char* bytes, size_t remaining)
    {
        const unsigned char first = bytes[0];
        if (first < 0x80) return 1;
        const size_t width = first >= 0xc2 && first <= 0xdf ? 2
            : first >= 0xe0 && first <= 0xef ? 3
            : first >= 0xf0 && first <= 0xf4 ? 4 : 0;
        if (width == 0 || width > remaining) return 0;
        for (size_t i = 1; i < width; ++i)
            if ((bytes[i] & 0xc0) != 0x80) return 0;
        if ((first == 0xe0 && bytes[1] < 0xa0) || (first == 0xed && bytes[1] >= 0xa0)
            || (first == 0xf0 && bytes[1] < 0x90) || (first == 0xf4 && bytes[1] >= 0x90)) return 0;
        return width;
    }

    bool IsPrintable(unsigned char value, size_t width)
    {
        return width != 0 && value >= 32 && value != 127;
    }

    std::string Offset(size_t offset)
    {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%08llX", static_cast<unsigned long long>(offset));
        return buffer;
    }
}

size_t preview::HexByteTextPosition(size_t byteInPage)
{
    const size_t column = byteInPage % HexRowBytes;
    return byteInPage / HexRowBytes * HexRowWidth + HexOffsetWidth
        + column * HexByteWidth + (column >= HexGroupBytes ? 1 : 0);
}

preview::Page preview::Render(const std::vector<unsigned char>& bytes, size_t index, Format format)
{
    Page page;
    page.count = bytes.size() / PageBytes + (bytes.size() % PageBytes != 0 ? 1 : 0);
    if (page.count == 0) return page;
    page.index = std::min(index, page.count - 1);
    page.offset = page.index * PageBytes;
    page.length = std::min(PageBytes, bytes.size() - page.offset);
    const size_t end = page.offset + page.length;
    page.text.reserve(page.length * 5);

    if (format == Format::Hex)
    {
        constexpr char digits[] = "0123456789ABCDEF";
        for (size_t row = page.offset; row < end; row += HexRowBytes)
        {
            page.text += Offset(row) + "  ";
            for (size_t column = 0; column < HexRowBytes; ++column)
            {
                if (row + column < end)
                {
                    const auto value = bytes[row + column];
                    page.text += digits[value >> 4];
                    page.text += digits[value & 15];
                    page.text += ' ';
                }
                else page.text += "   ";
                if (column + 1 == HexGroupBytes) page.text += ' ';
            }
            page.text += " |";
            for (size_t column = 0; column < HexRowBytes && row + column < end; ++column)
            {
                const auto value = bytes[row + column];
                page.text += value >= 32 && value <= 126 ? static_cast<char>(value) : '.';
            }
            page.text += "|\n";
        }
        return page;
    }

    for (size_t i = page.offset; i < end;)
    {
        const auto value = bytes[i];
        const size_t width = Utf8Width(bytes.data() + i, end - i);
        if (IsPrintable(value, width) || value == '\t' || value == '\n' || value == '\r')
            page.text.append(reinterpret_cast<const char*>(bytes.data() + i), width);
        else page.text += '.';
        i += width == 0 ? 1 : width;
    }
    return page;
}

std::vector<preview::StringRange> preview::IndexStrings(const std::vector<unsigned char>& bytes)
{
    if (bytes.size() > std::numeric_limits<std::uint32_t>::max())
        throw std::length_error("String index exceeds the supported file size");

    std::vector<StringRange> index;
    size_t runStart = 0;
    size_t characters = 0;
    auto flush = [&](size_t end) {
        if (characters >= 4)
            index.push_back({static_cast<std::uint32_t>(runStart), static_cast<std::uint32_t>(end - runStart)});
        characters = 0;
    };
    for (size_t i = 0; i < bytes.size();)
    {
        const size_t width = Utf8Width(bytes.data() + i, bytes.size() - i);
        if (IsPrintable(bytes[i], width))
        {
            if (characters == 0) runStart = i;
            ++characters;
        }
        else flush(i);
        i += width == 0 ? 1 : width;
    }
    flush(bytes.size());
    return index;
}

preview::StringMatch preview::FindString(const std::vector<unsigned char>& bytes,
                                         const std::vector<StringRange>& index,
                                         const std::string& query, size_t start)
{
    if (query.empty() || start >= bytes.size()) return {};
    auto row = std::lower_bound(index.begin(), index.end(), start,
        [](const StringRange& range, size_t offset) { return range.End() <= offset; });
    for (; row != index.end(); ++row)
    {
        const auto begin = std::max(start, static_cast<size_t>(row->offset));
        const std::string_view text(reinterpret_cast<const char*>(bytes.data() + begin), row->End() - begin);
        const size_t match = text.find(query);
        if (match != std::string_view::npos)
            return {static_cast<size_t>(row - index.begin()), begin + match};
    }
    return {};
}

size_t preview::Find(const std::vector<unsigned char>& bytes, const std::string& query, size_t start)
{
    if (query.empty() || start >= bytes.size()) return NotFound;
    const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    const size_t match = text.find(query, start);
    return match == std::string_view::npos ? NotFound : match;
}
