#include "ui_console/ConsoleFormatting.h"

#include <algorithm>

namespace xiangqi
{

namespace
{

bool isAnsiCsiFinal(const unsigned char ch)
{
    return ch >= 0x40 && ch <= 0x7E;
}

int utf8Length(const unsigned char lead)
{
    if ((lead & 0x80) == 0)
    {
        return 1;
    }
    if ((lead & 0xE0) == 0xC0)
    {
        return 2;
    }
    if ((lead & 0xF0) == 0xE0)
    {
        return 3;
    }
    if ((lead & 0xF8) == 0xF0)
    {
        return 4;
    }
    return 1;
}

unsigned int decodeUtf8(const std::string_view text, const size_t offset, const int length)
{
    const auto byte = [&](const size_t index)
    {
        return static_cast<unsigned int>(static_cast<unsigned char>(text[offset + index]));
    };

    switch (length)
    {
    case 2:
        return ((byte(0) & 0x1Fu) << 6) | (byte(1) & 0x3Fu);
    case 3:
        return ((byte(0) & 0x0Fu) << 12) | ((byte(1) & 0x3Fu) << 6) | (byte(2) & 0x3Fu);
    case 4:
        return ((byte(0) & 0x07u) << 18) | ((byte(1) & 0x3Fu) << 12) |
            ((byte(2) & 0x3Fu) << 6) | (byte(3) & 0x3Fu);
    default:
        return byte(0);
    }
}

bool isWideCodePoint(const unsigned int codepoint)
{
    return (codepoint >= 0x1100 && codepoint <= 0x115F) ||
        (codepoint >= 0x2E80 && codepoint <= 0xA4CF) ||
        (codepoint >= 0xAC00 && codepoint <= 0xD7A3) ||
        (codepoint >= 0xF900 && codepoint <= 0xFAFF) ||
        (codepoint >= 0xFE10 && codepoint <= 0xFE19) ||
        (codepoint >= 0xFE30 && codepoint <= 0xFE6F) ||
        (codepoint >= 0xFF00 && codepoint <= 0xFF60) ||
        (codepoint >= 0xFFE0 && codepoint <= 0xFFE6);
}

} // namespace

int consoleDisplayWidth(const std::string_view text)
{
    int width = 0;
    for (size_t index = 0; index < text.size();)
    {
        const auto ch = static_cast<unsigned char>(text[index]);
        // ANSI color escapes change terminal state but occupy no visible columns.
        if (ch == 0x1B && index + 1 < text.size() && text[index + 1] == '[')
        {
            index += 2;
            while (index < text.size() && !isAnsiCsiFinal(static_cast<unsigned char>(text[index])))
            {
                ++index;
            }
            if (index < text.size())
            {
                ++index;
            }
            continue;
        }

        if (ch < 0x20 || ch == 0x7F)
        {
            ++index;
            continue;
        }

        const int length = utf8Length(ch);
        if (index + static_cast<size_t>(length) > text.size())
        {
            ++width;
            ++index;
            continue;
        }

        const unsigned int codepoint = decodeUtf8(text, index, length);
        // CJK/full-width glyphs take two terminal columns in the console board.
        width += isWideCodePoint(codepoint) ? 2 : 1;
        index += static_cast<size_t>(std::max(length, 1));
    }
    return width;
}

std::string rightAlignConsoleCell(const std::string_view rendered, const int width)
{
    std::string result;
    const int visible_width = consoleDisplayWidth(rendered);
    if (visible_width < width)
    {
        result.assign(static_cast<size_t>(width - visible_width), ' ');
    }
    result.append(rendered.begin(), rendered.end());
    return result;
}

} // namespace xiangqi
