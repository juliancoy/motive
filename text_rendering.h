#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace motive {
namespace text {

// ============================================================================
// Font Bitmap Rendering (from fonts.h)
// ============================================================================

struct FontBitmap
{
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> pixels; // RGBA
};

struct FontRenderOptions
{
    std::string fontPath;
    bool bold = false;
    bool italic = false;
    int letterSpacing = 0;
};

struct TextOverlayStyle
{
    uint32_t textColor = 0xFFFFFFFF;
    uint32_t shadowColor = 0xC0000000;
    uint32_t outlineColor = 0xFF000000;
    uint32_t backgroundColor = 0xAA000000;
    bool drawBackground = true;
    bool drawShadow = true;
    bool drawOutline = false;
    int shadowOffsetX = 2;
    int shadowOffsetY = 2;
};

// Render the given text using the nofile.ttf font at the requested pixel height.
// Returns an RGBA bitmap (white text with alpha) positioned on the baseline.
FontBitmap renderText(const std::string &text, uint32_t pixelHeight);
FontBitmap renderText(const std::string &text, uint32_t pixelHeight, const FontRenderOptions& options);

// ============================================================================
// Overlay Bitmap Generation (from glyph.h)
// ============================================================================

struct OverlayBitmap
{
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t offsetX = 0;
    uint32_t offsetY = 0;
    std::vector<uint8_t> pixels;
};

OverlayBitmap buildLabeledOverlay(uint32_t referenceWidth,
                                  uint32_t referenceHeight,
                                  std::string_view label,
                                  float value);

OverlayBitmap buildFrameRateOverlay(uint32_t referenceWidth,
                                    uint32_t referenceHeight,
                                    float fps);
OverlayBitmap buildStyledTextOverlay(uint32_t referenceWidth,
                                     uint32_t referenceHeight,
                                     std::string_view text,
                                     uint32_t pixelHeight,
                                     const FontRenderOptions& fontOptions,
                                     const TextOverlayStyle& style);

} // namespace text
} // namespace motive

// Backwards compatibility aliases in global namespaces
namespace fonts {
    using FontBitmap = motive::text::FontBitmap;
    inline FontBitmap renderText(const std::string &text, uint32_t pixelHeight) {
        return motive::text::renderText(text, pixelHeight);
    }
}

namespace glyph {
    using OverlayBitmap = motive::text::OverlayBitmap;
    inline OverlayBitmap buildLabeledOverlay(uint32_t referenceWidth,
                                              uint32_t referenceHeight,
                                              std::string_view label,
                                              float value) {
        return motive::text::buildLabeledOverlay(referenceWidth, referenceHeight, label, value);
    }
    inline OverlayBitmap buildFrameRateOverlay(uint32_t referenceWidth,
                                                uint32_t referenceHeight,
                                                float fps) {
        return motive::text::buildFrameRateOverlay(referenceWidth, referenceHeight, fps);
    }
}
