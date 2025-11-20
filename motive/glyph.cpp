#include "glyph.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <string_view>
#include <unordered_map>

namespace glyph
{
namespace
{
    constexpr int kGlyphHeight = 7;
    constexpr int kCharacterSpacing = 1;
    constexpr int kSpaceWidth = 3;
    constexpr int kBaseMargin = 8;
    constexpr int kBasePadding = 4;

    struct GlyphPattern
    {
        int width;
        std::array<std::string_view, kGlyphHeight> rows;
    };

    const std::unordered_map<char, GlyphPattern> kGlyphPatterns = {
        {'0', {5, {" ### ", "#   #", "#  ##", "# # #", "##  #", "#   #", " ### "}}},
        {'1', {5, {"  #  ", " ##  ", "  #  ", "  #  ", "  #  ", "  #  ", " ### "}}},
        {'2', {5, {" ### ", "#   #", "    #", "   # ", "  #  ", " #   ", "#####"}}},
        {'3', {5, {"#### ", "    #", "  ## ", "    #", "    #", "#   #", " ### "}}},
        {'4', {5, {"   # ", "  ## ", " # # ", "#  # ", "#####", "   # ", "   # "}}},
        {'5', {5, {"#####", "#    ", "#### ", "    #", "    #", "#   #", " ### "}}},
        {'6', {5, {" ### ", "#   #", "#    ", "#### ", "#   #", "#   #", " ### "}}},
        {'7', {5, {"#####", "    #", "   # ", "  #  ", " #   ", " #   ", " #   "}}},
        {'8', {5, {" ### ", "#   #", "#   #", " ### ", "#   #", "#   #", " ### "}}},
        {'9', {5, {" ### ", "#   #", "#   #", " ####", "    #", "#   #", " ### "}}},
        {'F', {5, {"#####", "#    ", "###  ", "#    ", "#    ", "#    ", "#    "}}},
        {'P', {5, {"#### ", "#   #", "#   #", "#### ", "#    ", "#    ", "#    "}}},
        {'S', {5, {" ####", "#    ", "#    ", " ### ", "    #", "    #", "#### "}}},
        {'A', {5, {" ### ", "#   #", "#   #", "#####", "#   #", "#   #", "#   #"}}},
        {'E', {5, {"#####", "#    ", "###  ", "#    ", "#    ", "#    ", "#####"}}},
        {'H', {5, {"#   #", "#   #", "#   #", "#####", "#   #", "#   #", "#   #"}}},
        {'L', {5, {"#    ", "#    ", "#    ", "#    ", "#    ", "#    ", "#####"}}},
        {'O', {5, {" ### ", "#   #", "#   #", "#   #", "#   #", "#   #", " ### "}}},
        {':', {3, {"   ", " # ", "   ", "   ", " # ", "   ", "   "}}},
        {'.', {3, {"   ", "   ", "   ", "   ", "   ", " ##", " ##"}}},
        {'-', {5, {"     ", "     ", "     ", " ### ", "     ", "     ", "     "}}},
    };

    char normalizeGlyphChar(char c)
    {
        if (c >= 'a' && c <= 'z')
        {
            return static_cast<char>(c - 32);
        }
        return c;
    }

    int measureTextWidth(const std::string &text)
    {
        int width = 0;
        for (char c : text)
        {
            if (c == ' ')
            {
                width += kSpaceWidth + kCharacterSpacing;
                continue;
            }
            const char key = normalizeGlyphChar(c);
            auto it = kGlyphPatterns.find(key);
            if (it != kGlyphPatterns.end())
            {
                width += it->second.width + kCharacterSpacing;
            }
            else
            {
                width += kSpaceWidth + kCharacterSpacing;
            }
        }
        if (width > 0)
        {
            width -= kCharacterSpacing;
        }
        return width;
    }

    void fillOverlayBitmap(const std::string &text,
                           uint32_t pixelScale,
                           OverlayBitmap &bitmap)
    {
        if (bitmap.width == 0 || bitmap.height == 0)
        {
            return;
        }

        bitmap.pixels.assign(static_cast<size_t>(bitmap.width) * bitmap.height * 4, 0);

        auto setPixel = [&](int px, int py, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
            if (px < 0 || py < 0 || px >= static_cast<int>(bitmap.width) || py >= static_cast<int>(bitmap.height))
            {
                return;
            }
            const size_t index = (static_cast<size_t>(py) * bitmap.width + static_cast<size_t>(px)) * 4;
            bitmap.pixels[index + 0] = r;
            bitmap.pixels[index + 1] = g;
            bitmap.pixels[index + 2] = b;
            bitmap.pixels[index + 3] = a;
        };

        for (uint32_t y = 0; y < bitmap.height; ++y)
        {
            for (uint32_t x = 0; x < bitmap.width; ++x)
            {
                setPixel(static_cast<int>(x), static_cast<int>(y), 0, 0, 0, 200);
            }
        }

        const int scaledPadding = kBasePadding * static_cast<int>(pixelScale);
        const int scaledCharacterSpacing = kCharacterSpacing * static_cast<int>(pixelScale);
        const int scaledSpaceWidth = kSpaceWidth * static_cast<int>(pixelScale);

        int penX = scaledPadding;
        const int penY = scaledPadding;
        for (char c : text)
        {
            if (c == ' ')
            {
                penX += scaledSpaceWidth + scaledCharacterSpacing;
                continue;
            }
            const char key = normalizeGlyphChar(c);
            auto it = kGlyphPatterns.find(key);
            if (it == kGlyphPatterns.end())
            {
                penX += scaledSpaceWidth + scaledCharacterSpacing;
                continue;
            }
            const GlyphPattern &glyph = it->second;
            for (int row = 0; row < kGlyphHeight; ++row)
            {
                const std::string_view pattern = glyph.rows[row];
                for (int col = 0; col < glyph.width && col < static_cast<int>(pattern.size()); ++col)
                {
                    if (pattern[col] != ' ')
                    {
                        for (uint32_t sy = 0; sy < pixelScale; ++sy)
                        {
                            for (uint32_t sx = 0; sx < pixelScale; ++sx)
                            {
                                setPixel(penX + col * static_cast<int>(pixelScale) + static_cast<int>(sx),
                                         penY + row * static_cast<int>(pixelScale) + static_cast<int>(sy),
                                         255, 255, 255, 255);
                            }
                        }
                    }
                }
            }
            penX += glyph.width * static_cast<int>(pixelScale) + scaledCharacterSpacing;
            if (penX >= static_cast<int>(bitmap.width))
            {
                break;
            }
        }
    }
} // namespace

OverlayBitmap buildFrameRateOverlay(uint32_t referenceWidth,
                                    uint32_t referenceHeight,
                                    float fps)
{
    OverlayBitmap bitmap{};
    if (referenceWidth == 0 || referenceHeight == 0)
    {
        return bitmap;
    }

    char text[32];
    if (fps > 0.0f)
    {
        std::snprintf(text, sizeof(text), "FPS %.1f", fps);
    }
    else
    {
        std::snprintf(text, sizeof(text), "FPS ----");
    }

    int scaleFromWidth = static_cast<int>(referenceWidth / 640);
    int scaleFromHeight = static_cast<int>(referenceHeight / 360);
    scaleFromWidth = std::max(1, scaleFromWidth);
    scaleFromHeight = std::max(1, scaleFromHeight);
    const uint32_t pixelScale = static_cast<uint32_t>(std::max(1, std::min(scaleFromWidth, scaleFromHeight)));

    const int textWidth = std::max(0, measureTextWidth(text));
    const uint32_t overlayWidth = static_cast<uint32_t>(textWidth * static_cast<int>(pixelScale) + kBasePadding * 2 * static_cast<int>(pixelScale));
    const uint32_t overlayHeight = static_cast<uint32_t>(kGlyphHeight * static_cast<int>(pixelScale) + kBasePadding * 2 * static_cast<int>(pixelScale));

    bitmap.width = std::min(overlayWidth, referenceWidth);
    bitmap.height = std::min(overlayHeight, referenceHeight);

    const uint32_t scaledMargin = static_cast<uint32_t>(kBaseMargin * static_cast<int>(pixelScale));
    const uint32_t maxOffsetX = referenceWidth > bitmap.width ? referenceWidth - bitmap.width : 0;
    const uint32_t maxOffsetY = referenceHeight > bitmap.height ? referenceHeight - bitmap.height : 0;
    bitmap.offsetX = std::min(scaledMargin, maxOffsetX);
    bitmap.offsetY = std::min(scaledMargin, maxOffsetY);

    fillOverlayBitmap(text, pixelScale, bitmap);
    return bitmap;
}

} // namespace glyph
