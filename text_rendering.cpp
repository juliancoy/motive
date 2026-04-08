#include "text_rendering.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#include <iomanip>

// FreeType headers
#include <ft2build.h>
#include FT_FREETYPE_H

namespace motive {
namespace text {

// ============================================================================
// Font Implementation
// ============================================================================

namespace {
    FT_Library getFreeTypeLibrary()
    {
        static FT_Library library = nullptr;
        if (!library)
        {
            FT_Error error = FT_Init_FreeType(&library);
            if (error)
            {
                return nullptr;
            }
        }
        return library;
    }

    FT_Face getFontFace()
    {
        static FT_Face face = nullptr;
        if (!face)
        {
            FT_Library library = getFreeTypeLibrary();
            if (!library)
            {
                return nullptr;
            }
            // Load from the nofile.ttf embedded or file
            FT_Error error = FT_New_Face(library, "nofile.ttf", 0, &face);
            if (error)
            {
                return nullptr;
            }
        }
        return face;
    }
}

FontBitmap renderText(const std::string &text, uint32_t pixelHeight)
{
    FontBitmap result;
    FT_Face face = getFontFace();
    if (!face)
    {
        return result;
    }

    FT_Error error = FT_Set_Pixel_Sizes(face, 0, pixelHeight);
    if (error)
    {
        return result;
    }

    // Calculate total width and max height
    int totalWidth = 0;
    int maxHeight = 0;
    int minY = 0;

    for (char c : text)
    {
        FT_UInt glyphIndex = FT_Get_Char_Index(face, c);
        error = FT_Load_Glyph(face, glyphIndex, FT_LOAD_DEFAULT);
        if (error)
            continue;

        error = FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);
        if (error)
            continue;

        totalWidth += face->glyph->advance.x >> 6;
        int height = face->glyph->bitmap.rows;
        int yOffset = face->glyph->bitmap_top;
        int yBottom = yOffset - static_cast<int>(height);
        maxHeight = std::max(maxHeight, yOffset);
        minY = std::min(minY, yBottom);
    }

    int bitmapHeight = maxHeight - minY;
    if (bitmapHeight <= 0)
    {
        bitmapHeight = pixelHeight;
    }

    result.width = totalWidth;
    result.height = bitmapHeight;
    result.pixels.resize(result.width * result.height * 4, 0);

    // Render each character
    int xOffset = 0;
    for (char c : text)
    {
        FT_UInt glyphIndex = FT_Get_Char_Index(face, c);
        error = FT_Load_Glyph(face, glyphIndex, FT_LOAD_DEFAULT);
        if (error)
            continue;

        error = FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);
        if (error)
            continue;

        FT_Bitmap &bitmap = face->glyph->bitmap;
        int yOffset = maxHeight - face->glyph->bitmap_top;

        for (unsigned int y = 0; y < bitmap.rows; ++y)
        {
            for (unsigned int x = 0; x < bitmap.width; ++x)
            {
                int dstX = xOffset + x;
                int dstY = yOffset + y;
                if (dstX >= 0 && dstX < static_cast<int>(result.width) &&
                    dstY >= 0 && dstY < static_cast<int>(result.height))
                {
                    uint8_t alpha = bitmap.buffer[y * bitmap.pitch + x];
                    size_t idx = (dstY * result.width + dstX) * 4;
                    result.pixels[idx + 0] = 255; // R
                    result.pixels[idx + 1] = 255; // G
                    result.pixels[idx + 2] = 255; // B
                    result.pixels[idx + 3] = alpha; // A
                }
            }
        }

        xOffset += face->glyph->advance.x >> 6;
    }

    return result;
}

// ============================================================================
// Overlay Implementation
// ============================================================================

namespace {
    constexpr uint32_t kOverlayPadding = 8;
    constexpr uint32_t kOverlayBgColor = 0x000000AA; // Semi-transparent black
    constexpr uint32_t kOverlayTextColor = 0xFFFFFFFF; // White

    void drawRect(std::vector<uint8_t> &pixels,
                  uint32_t width,
                  uint32_t height,
                  uint32_t x,
                  uint32_t y,
                  uint32_t w,
                  uint32_t h,
                  uint32_t color)
    {
        uint8_t b = (color >> 0) & 0xFF;
        uint8_t g = (color >> 8) & 0xFF;
        uint8_t r = (color >> 16) & 0xFF;
        uint8_t a = (color >> 24) & 0xFF;

        for (uint32_t py = y; py < y + h && py < height; ++py)
        {
            for (uint32_t px = x; px < x + w && px < width; ++px)
            {
                size_t idx = (py * width + px) * 4;
                // Simple alpha blend
                uint8_t dstA = pixels[idx + 3];
                uint8_t srcA = a;
                uint8_t outA = srcA + (dstA * (255 - srcA) / 255);
                
                if (outA > 0)
                {
                    pixels[idx + 0] = (b * srcA + pixels[idx + 0] * (255 - srcA) / 255) * 255 / outA;
                    pixels[idx + 1] = (g * srcA + pixels[idx + 1] * (255 - srcA) / 255) * 255 / outA;
                    pixels[idx + 2] = (r * srcA + pixels[idx + 2] * (255 - srcA) / 255) * 255 / outA;
                    pixels[idx + 3] = outA;
                }
            }
        }
    }

    void drawText(std::vector<uint8_t> &pixels,
                  uint32_t width,
                  uint32_t height,
                  uint32_t x,
                  uint32_t y,
                  const std::string &text,
                  uint32_t pixelHeight)
    {
        FontBitmap bitmap = renderText(text, pixelHeight);
        if (bitmap.pixels.empty())
            return;

        for (uint32_t row = 0; row < bitmap.height; ++row)
        {
            for (uint32_t col = 0; col < bitmap.width; ++col)
            {
                uint32_t dstX = x + col;
                uint32_t dstY = y + row;
                if (dstX < width && dstY < height)
                {
                    size_t srcIdx = (row * bitmap.width + col) * 4;
                    size_t dstIdx = (dstY * width + dstX) * 4;
                    uint8_t alpha = bitmap.pixels[srcIdx + 3];
                    if (alpha > 0)
                    {
                        pixels[dstIdx + 0] = bitmap.pixels[srcIdx + 0];
                        pixels[dstIdx + 1] = bitmap.pixels[srcIdx + 1];
                        pixels[dstIdx + 2] = bitmap.pixels[srcIdx + 2];
                        pixels[dstIdx + 3] = alpha;
                    }
                }
            }
        }
    }
}

OverlayBitmap buildLabeledOverlay(uint32_t referenceWidth,
                                  uint32_t referenceHeight,
                                  std::string_view label,
                                  float value)
{
    OverlayBitmap result;
    
    // Determine overlay size based on reference
    uint32_t overlayWidth = std::min<uint32_t>(300, referenceWidth / 3);
    uint32_t overlayHeight = std::min<uint32_t>(80, referenceHeight / 10);
    
    result.width = overlayWidth;
    result.height = overlayHeight;
    result.offsetX = kOverlayPadding;
    result.offsetY = kOverlayPadding;
    result.pixels.resize(overlayWidth * overlayHeight * 4, 0);

    // Draw background
    drawRect(result.pixels, overlayWidth, overlayHeight, 0, 0, overlayWidth, overlayHeight, kOverlayBgColor);

    // Format text
    std::ostringstream oss;
    oss << label << ": " << std::fixed << std::setprecision(2) << value;
    std::string text = oss.str();

    // Draw text
    uint32_t textHeight = overlayHeight / 2;
    uint32_t textY = (overlayHeight - textHeight) / 2;
    drawText(result.pixels, overlayWidth, overlayHeight, kOverlayPadding, textY, text, textHeight);

    return result;
}

OverlayBitmap buildFrameRateOverlay(uint32_t referenceWidth,
                                    uint32_t referenceHeight,
                                    float fps)
{
    return buildLabeledOverlay(referenceWidth, referenceHeight, "FPS", fps);
}

} // namespace text
} // namespace motive
