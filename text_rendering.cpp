#include "text_rendering.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <unordered_map>

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

    FT_Face getFontFace(const std::string& fontPath)
    {
        static std::unordered_map<std::string, FT_Face> faces;
        const std::string key = fontPath.empty() ? std::string("nofile.ttf") : fontPath;
        const auto it = faces.find(key);
        if (it != faces.end())
        {
            return it->second;
        }

        FT_Library library = getFreeTypeLibrary();
        if (!library)
        {
            return nullptr;
        }
        FT_Face face = nullptr;
        FT_Error error = FT_New_Face(library, key.c_str(), 0, &face);
        if (error)
        {
            return nullptr;
        }
        faces.emplace(key, face);
        return face;
    }

    void blendPixel(uint8_t* dst, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        const float srcA = static_cast<float>(a) / 255.0f;
        const float dstA = static_cast<float>(dst[3]) / 255.0f;
        const float outA = srcA + dstA * (1.0f - srcA);
        if (outA <= 0.0f)
        {
            return;
        }
        dst[0] = static_cast<uint8_t>(((static_cast<float>(r) * srcA) + (static_cast<float>(dst[0]) * dstA * (1.0f - srcA))) / outA);
        dst[1] = static_cast<uint8_t>(((static_cast<float>(g) * srcA) + (static_cast<float>(dst[1]) * dstA * (1.0f - srcA))) / outA);
        dst[2] = static_cast<uint8_t>(((static_cast<float>(b) * srcA) + (static_cast<float>(dst[2]) * dstA * (1.0f - srcA))) / outA);
        dst[3] = static_cast<uint8_t>(outA * 255.0f);
    }

    void drawTintedBitmap(std::vector<uint8_t>& pixels,
                          uint32_t width,
                          uint32_t height,
                          int x,
                          int y,
                          const FontBitmap& bitmap,
                          uint32_t color)
    {
        const uint8_t b = static_cast<uint8_t>((color >> 0) & 0xFFu);
        const uint8_t g = static_cast<uint8_t>((color >> 8) & 0xFFu);
        const uint8_t r = static_cast<uint8_t>((color >> 16) & 0xFFu);
        const uint8_t aScale = static_cast<uint8_t>((color >> 24) & 0xFFu);
        for (uint32_t row = 0; row < bitmap.height; ++row)
        {
            const int dstY = y + static_cast<int>(row);
            if (dstY < 0 || dstY >= static_cast<int>(height))
            {
                continue;
            }
            for (uint32_t col = 0; col < bitmap.width; ++col)
            {
                const int dstX = x + static_cast<int>(col);
                if (dstX < 0 || dstX >= static_cast<int>(width))
                {
                    continue;
                }
                const size_t srcIdx = (static_cast<size_t>(row) * bitmap.width + col) * 4u;
                const uint8_t srcA = static_cast<uint8_t>((static_cast<uint16_t>(bitmap.pixels[srcIdx + 3]) * aScale) / 255u);
                if (srcA == 0)
                {
                    continue;
                }
                const size_t dstIdx = (static_cast<size_t>(dstY) * width + static_cast<size_t>(dstX)) * 4u;
                blendPixel(&pixels[dstIdx], r, g, b, srcA);
            }
        }
    }
}

FontBitmap renderText(const std::string &text, uint32_t pixelHeight)
{
    return renderText(text, pixelHeight, FontRenderOptions{});
}

FontBitmap renderText(const std::string &text, uint32_t pixelHeight, const FontRenderOptions& options)
{
    FontBitmap result;
    FT_Face face = getFontFace(options.fontPath);
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

    FT_UInt prevGlyphIndex = 0;
    const bool hasKerning = FT_HAS_KERNING(face);
    for (char c : text)
    {
        FT_UInt glyphIndex = FT_Get_Char_Index(face, c);
        if (hasKerning && prevGlyphIndex != 0 && glyphIndex != 0)
        {
            FT_Vector kerning{};
            if (FT_Get_Kerning(face, prevGlyphIndex, glyphIndex, FT_KERNING_DEFAULT, &kerning) == 0)
            {
                totalWidth += (kerning.x >> 6);
            }
        }
        error = FT_Load_Glyph(face, glyphIndex, FT_LOAD_DEFAULT);
        if (error)
        {
            prevGlyphIndex = glyphIndex;
            continue;
        }

        error = FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);
        if (error)
            continue;

        totalWidth += (face->glyph->advance.x >> 6) + options.letterSpacing;
        int height = face->glyph->bitmap.rows;
        int yOffset = face->glyph->bitmap_top;
        int yBottom = yOffset - static_cast<int>(height);
        maxHeight = std::max(maxHeight, yOffset);
        minY = std::min(minY, yBottom);
        prevGlyphIndex = glyphIndex;
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
    prevGlyphIndex = 0;
    for (char c : text)
    {
        FT_UInt glyphIndex = FT_Get_Char_Index(face, c);
        if (hasKerning && prevGlyphIndex != 0 && glyphIndex != 0)
        {
            FT_Vector kerning{};
            if (FT_Get_Kerning(face, prevGlyphIndex, glyphIndex, FT_KERNING_DEFAULT, &kerning) == 0)
            {
                xOffset += (kerning.x >> 6);
            }
        }
        error = FT_Load_Glyph(face, glyphIndex, FT_LOAD_DEFAULT);
        if (error)
        {
            prevGlyphIndex = glyphIndex;
            continue;
        }

        error = FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);
        if (error)
            continue;

        FT_Bitmap &bitmap = face->glyph->bitmap;
        int yOffset = maxHeight - face->glyph->bitmap_top;

        for (unsigned int y = 0; y < bitmap.rows; ++y)
        {
            for (unsigned int x = 0; x < bitmap.width; ++x)
            {
                int dstX = xOffset + static_cast<int>(x);
                if (options.italic)
                {
                    const int rowFromBottom = static_cast<int>(bitmap.rows - y);
                    dstX += rowFromBottom / 5;
                }
                int dstY = yOffset + y;
                if (dstX >= 0 && dstX < static_cast<int>(result.width) &&
                    dstY >= 0 && dstY < static_cast<int>(result.height))
                {
                    uint8_t alpha = bitmap.buffer[y * bitmap.pitch + x];
                    if (options.bold && x + 1 < bitmap.width)
                    {
                        alpha = std::max(alpha, bitmap.buffer[y * bitmap.pitch + (x + 1)]);
                    }
                    size_t idx = (dstY * result.width + dstX) * 4;
                    result.pixels[idx + 0] = 255; // R
                    result.pixels[idx + 1] = 255; // G
                    result.pixels[idx + 2] = 255; // B
                    result.pixels[idx + 3] = alpha; // A
                }
            }
        }

        xOffset += (face->glyph->advance.x >> 6) + options.letterSpacing;
        prevGlyphIndex = glyphIndex;
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
        const uint8_t r = static_cast<uint8_t>((color >> 16) & 0xFFu);
        const uint8_t g = static_cast<uint8_t>((color >> 8) & 0xFFu);
        const uint8_t b = static_cast<uint8_t>((color >> 0) & 0xFFu);
        const uint8_t a = static_cast<uint8_t>((color >> 24) & 0xFFu);

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
                    pixels[idx + 0] = (r * srcA + pixels[idx + 0] * (255 - srcA) / 255) * 255 / outA;
                    pixels[idx + 1] = (g * srcA + pixels[idx + 1] * (255 - srcA) / 255) * 255 / outA;
                    pixels[idx + 2] = (b * srcA + pixels[idx + 2] * (255 - srcA) / 255) * 255 / outA;
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

OverlayBitmap buildStyledTextOverlay(uint32_t referenceWidth,
                                     uint32_t referenceHeight,
                                     std::string_view text,
                                     uint32_t pixelHeight,
                                     const FontRenderOptions& fontOptions,
                                     const TextOverlayStyle& style)
{
    OverlayBitmap result;
    const uint32_t overlayWidth = std::max<uint32_t>(1u, referenceWidth);
    const uint32_t overlayHeight = std::max<uint32_t>(1u, referenceHeight);
    result.width = overlayWidth;
    result.height = overlayHeight;
    result.offsetX = 0;
    result.offsetY = 0;
    result.pixels.assign(static_cast<size_t>(overlayWidth) * static_cast<size_t>(overlayHeight) * 4u, 0u);

    if (text.empty())
    {
        return result;
    }

    const FontBitmap bitmap = renderText(std::string(text), pixelHeight, fontOptions);
    if (bitmap.width == 0 || bitmap.height == 0 || bitmap.pixels.empty())
    {
        return result;
    }

    const uint32_t panelPadding = 12u;
    const uint32_t panelW = std::min<uint32_t>(overlayWidth, bitmap.width + panelPadding * 2u);
    const uint32_t panelH = std::min<uint32_t>(overlayHeight, bitmap.height + panelPadding * 2u);
    const uint32_t panelX = (overlayWidth > panelW) ? (overlayWidth - panelW) / 2u : 0u;
    const uint32_t panelY = (overlayHeight > panelH) ? (overlayHeight - panelH) / 2u : 0u;
    if (style.drawBackground)
    {
        drawRect(result.pixels, overlayWidth, overlayHeight, panelX, panelY, panelW, panelH, style.backgroundColor);
    }

    const int textX = static_cast<int>(panelX + panelPadding);
    const int textY = static_cast<int>(panelY + panelPadding);
    if (style.drawShadow)
    {
        drawTintedBitmap(result.pixels, overlayWidth, overlayHeight,
                         textX + style.shadowOffsetX, textY + style.shadowOffsetY,
                         bitmap, style.shadowColor);
    }
    if (style.drawOutline)
    {
        for (int oy = -1; oy <= 1; ++oy)
        {
            for (int ox = -1; ox <= 1; ++ox)
            {
                if (ox == 0 && oy == 0)
                {
                    continue;
                }
                drawTintedBitmap(result.pixels, overlayWidth, overlayHeight, textX + ox, textY + oy, bitmap, style.outlineColor);
            }
        }
    }
    drawTintedBitmap(result.pixels, overlayWidth, overlayHeight, textX, textY, bitmap, style.textColor);
    return result;
}

} // namespace text
} // namespace motive
