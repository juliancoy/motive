#include "grading.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <cstdlib>

#include "engine.h"
#include "glyph.h"
#include "overlay.hpp"

namespace
{
float clamp01(float v)
{
    return std::clamp(v, 0.0f, 1.0f);
}

float normalizeExposure(float exposure)
{
    return clamp01((exposure + 2.0f) / 4.0f); // -2..2 -> 0..1
}

float denormalizeExposure(float norm)
{
    return std::clamp(norm * 4.0f - 2.0f, -4.0f, 4.0f);
}

float normalizeContrast(float contrast)
{
    // 0.5..2.0
    return clamp01((contrast - 0.5f) / 1.5f);
}

float denormalizeContrast(float norm)
{
    return std::clamp(0.5f + norm * 1.5f, 0.1f, 3.0f);
}

float normalizeSaturation(float sat)
{
    // 0..2
    return clamp01(sat * 0.5f);
}

float denormalizeSaturation(float norm)
{
    return std::clamp(norm * 2.0f, 0.0f, 3.0f);
}

void drawRect(std::vector<uint8_t>& buf,
              uint32_t width,
              uint32_t height,
              uint32_t x0,
              uint32_t y0,
              uint32_t x1,
              uint32_t y1,
              uint8_t r,
              uint8_t g,
              uint8_t b,
              uint8_t a)
{
    x0 = std::min(x0, width);
    x1 = std::min(x1, width);
    y0 = std::min(y0, height);
    y1 = std::min(y1, height);
    for (uint32_t y = y0; y < y1; ++y)
    {
        for (uint32_t x = x0; x < x1; ++x)
        {
            size_t idx = (static_cast<size_t>(y) * width + x) * 4;
            buf[idx + 0] = r;
            buf[idx + 1] = g;
            buf[idx + 2] = b;
            buf[idx + 3] = a;
        }
    }
}

void drawHandle(std::vector<uint8_t>& buf,
                uint32_t width,
                uint32_t height,
                float cx,
                float cy,
                float radius,
                uint8_t r,
                uint8_t g,
                uint8_t b,
                uint8_t a)
{
    int minX = std::max(0, static_cast<int>(std::floor(cx - radius - 1)));
    int maxX = std::min(static_cast<int>(width), static_cast<int>(std::ceil(cx + radius + 1)));
    int minY = std::max(0, static_cast<int>(std::floor(cy - radius - 1)));
    int maxY = std::min(static_cast<int>(height), static_cast<int>(std::ceil(cy + radius + 1)));
    float rad2 = radius * radius;
    for (int y = minY; y < maxY; ++y)
    {
        for (int x = minX; x < maxX; ++x)
        {
            float dx = static_cast<float>(x) - cx;
            float dy = static_cast<float>(y) - cy;
            float dist2 = dx * dx + dy * dy;
            if (dist2 <= rad2)
            {
                size_t idx = (static_cast<size_t>(y) * width + static_cast<uint32_t>(x)) * 4;
                buf[idx + 0] = r;
                buf[idx + 1] = g;
                buf[idx + 2] = b;
                buf[idx + 3] = a;
            }
        }
    }
}
} // namespace

namespace grading
{

bool buildGradingOverlay(Engine* engine,
                         const GradingSettings& settings,
                         overlay::ImageResource& image,
                         OverlayImageInfo& info,
                         uint32_t fbWidth,
                         uint32_t fbHeight,
                         SliderLayout& layout,
                         bool previewEnabled)
{
    if (layout.labels.empty())
    {
        layout.labels = {"Exposure", "Contrast", "Saturation",
                         "Shadows R", "Shadows G", "Shadows B",
                         "Midtones R", "Midtones G", "Midtones B",
                         "Highlights R", "Highlights G", "Highlights B"};
    }

    if (fbWidth == 0 || fbHeight == 0)
    {
        return false;
    }
    layout.width = 420;
    layout.height = 520;
    layout.margin = 20;
    layout.barHeight = 20;
    layout.barYStart = 32;
    layout.rowSpacing = 12;
    layout.handleRadius = 10;

    std::vector<uint8_t> pixels(static_cast<size_t>(layout.width) * layout.height * 4, 0);
    drawRect(pixels, layout.width, layout.height, 0, 0, layout.width, layout.height, 0, 0, 0, 180);

    const uint32_t padding = 12;
    const uint32_t barWidth = layout.width - padding * 2;
    const uint32_t barYStart = layout.barYStart;

    struct SliderDesc
    {
        float norm;
        uint32_t color[3];
    };
    SliderDesc sliders[12] = {
        {normalizeExposure(settings.exposure), {255, 180, 80}},
        {normalizeContrast(settings.contrast), {80, 200, 255}},
        {normalizeSaturation(settings.saturation), {180, 255, 160}},
        {clamp01(settings.shadows.r * 0.5f), {255, 120, 120}},
        {clamp01(settings.shadows.g * 0.5f), {120, 255, 120}},
        {clamp01(settings.shadows.b * 0.5f), {120, 120, 255}},
        {clamp01(settings.midtones.r * 0.5f), {255, 120, 120}},
        {clamp01(settings.midtones.g * 0.5f), {120, 255, 120}},
        {clamp01(settings.midtones.b * 0.5f), {120, 120, 255}},
        {clamp01(settings.highlights.r * 0.5f), {255, 120, 120}},
        {clamp01(settings.highlights.g * 0.5f), {120, 255, 120}},
        {clamp01(settings.highlights.b * 0.5f), {120, 120, 255}},
    };

    for (int i = 0; i < 12; ++i)
    {
        uint32_t y0 = barYStart + i * (layout.barHeight + layout.rowSpacing);
        uint32_t y1 = y0 + layout.barHeight;
        uint32_t x0 = padding;
        uint32_t x1 = padding + barWidth;
        drawRect(pixels, layout.width, layout.height, x0, y0, x1, y1, 40, 40, 40, 255);

        uint32_t fillW = static_cast<uint32_t>(std::round(sliders[i].norm * static_cast<float>(barWidth)));
        drawRect(pixels, layout.width, layout.height, x0, y0, x0 + fillW, y1,
                 sliders[i].color[0], sliders[i].color[1], sliders[i].color[2], 255);

        float handleX = static_cast<float>(x0 + fillW);
        float handleY = static_cast<float>(y0 + layout.barHeight * 0.5f);
        drawHandle(pixels, layout.width, layout.height, handleX, handleY, static_cast<float>(layout.handleRadius),
                   255, 255, 255, 255);

        // Label text drawn above the bar
        glyph::OverlayBitmap label = glyph::buildLabeledOverlay(layout.width, layout.height, layout.labels[i], 0.0f);
        if (!label.pixels.empty() && label.width > 0 && label.height > 0)
        {
            uint32_t textX = x0;
            uint32_t textY = y0 >= label.height + 2 ? y0 - label.height - 2 : 0;
            for (uint32_t ty = 0; ty < label.height && (textY + ty) < layout.height; ++ty)
            {
                for (uint32_t tx = 0; tx < label.width && (textX + tx) < layout.width; ++tx)
                {
                    size_t srcIdx = (static_cast<size_t>(ty) * label.width + tx) * 4;
                    size_t dstIdx = (static_cast<size_t>(textY + ty) * layout.width + (textX + tx)) * 4;
                    uint8_t a = label.pixels[srcIdx + 3];
                    if (a == 0)
                    {
                        continue;
                    }
                    pixels[dstIdx + 0] = label.pixels[srcIdx + 0];
                    pixels[dstIdx + 1] = label.pixels[srcIdx + 1];
                    pixels[dstIdx + 2] = label.pixels[srcIdx + 2];
                    pixels[dstIdx + 3] = std::max<uint8_t>(pixels[dstIdx + 3], a);
                }
            }
        }
    }

    // Reset button near the bottom
    const uint32_t buttonPadding = 16;
    const uint32_t buttonWidth = layout.resetWidth;
    const uint32_t buttonHeight = layout.resetHeight;
    const uint32_t totalButtonsWidth = layout.resetWidth + layout.saveWidth + layout.previewWidth + buttonPadding * 2;
    const uint32_t buttonStartX = (layout.width > totalButtonsWidth) ? (layout.width - totalButtonsWidth) / 2 : 0;
    const uint32_t buttonY = layout.height > (buttonHeight + buttonPadding) ? layout.height - buttonHeight - buttonPadding : 0;

    layout.resetX0 = buttonStartX;
    layout.resetX1 = buttonStartX + layout.resetWidth;
    layout.resetY0 = buttonY;
    layout.resetY1 = buttonY + layout.resetHeight;

    layout.saveX0 = buttonStartX + layout.resetWidth + buttonPadding;
    layout.saveX1 = layout.saveX0 + layout.saveWidth;
    layout.saveY0 = buttonY;
    layout.saveY1 = buttonY + layout.saveHeight;
    layout.previewX0 = layout.saveX1 + buttonPadding;
    layout.previewX1 = layout.previewX0 + layout.previewWidth;
    layout.previewY0 = buttonY;
    layout.previewY1 = buttonY + layout.previewHeight;

    auto drawButton = [&](uint32_t x0, uint32_t y0, uint32_t w, uint32_t h, const char* text) {
        drawRect(pixels, layout.width, layout.height, x0, y0, x0 + w, y0 + h, 70, 70, 70, 255);
        drawRect(pixels, layout.width, layout.height, x0 + 2, y0 + 2, x0 + w - 2, y0 + h - 2, 30, 30, 30, 255);
        glyph::OverlayBitmap textBmp = glyph::buildLabeledOverlay(layout.width, layout.height, text, 0.0f);
        if (!textBmp.pixels.empty())
        {
            uint32_t textX = x0 + (w > textBmp.width ? (w - textBmp.width) / 2 : 0);
            uint32_t textY = y0 + (h > textBmp.height ? (h - textBmp.height) / 2 : 0);
            for (uint32_t ty = 0; ty < textBmp.height && (textY + ty) < layout.height; ++ty)
            {
                for (uint32_t tx = 0; tx < textBmp.width && (textX + tx) < layout.width; ++tx)
                {
                    size_t srcIdx = (static_cast<size_t>(ty) * textBmp.width + tx) * 4;
                    size_t dstIdx = (static_cast<size_t>(textY + ty) * layout.width + (textX + tx)) * 4;
                    uint8_t a = textBmp.pixels[srcIdx + 3];
                    if (a == 0)
                    {
                        continue;
                    }
                    pixels[dstIdx + 0] = textBmp.pixels[srcIdx + 0];
                    pixels[dstIdx + 1] = textBmp.pixels[srcIdx + 1];
                    pixels[dstIdx + 2] = textBmp.pixels[srcIdx + 2];
                    pixels[dstIdx + 3] = std::max<uint8_t>(pixels[dstIdx + 3], a);
                }
            }
        }
    };

    drawButton(layout.resetX0, layout.resetY0, layout.resetWidth, layout.resetHeight, "Reset");
    drawButton(layout.saveX0, layout.saveY0, layout.saveWidth, layout.saveHeight, "Save");
    drawButton(layout.previewX0,
               layout.previewY0,
               layout.previewWidth,
               layout.previewHeight,
               previewEnabled ? "Preview On" : "Preview Off");

    const uint32_t overlayX = (fbWidth > layout.width) ? (fbWidth - layout.width) / 2 : 0;
    const uint32_t overlayY = (fbHeight > layout.height + layout.margin) ? fbHeight - layout.height - layout.margin : 0;
    layout.offset = {static_cast<int32_t>(overlayX), static_cast<int32_t>(overlayY)};

    if (!overlay::uploadImageData(engine,
                                  image,
                                  pixels.data(),
                                  pixels.size(),
                                  layout.width,
                                  layout.height,
                                  VK_FORMAT_R8G8B8A8_UNORM))
    {
        return false;
    }

    info.overlay.view = image.view;
    info.overlay.sampler = VK_NULL_HANDLE; // sampler set by caller
    info.extent = {layout.width, layout.height};
    info.offset = layout.offset;
    info.enabled = true;
    return true;
}

bool handleOverlayClick(const SliderLayout& layout,
                        double cursorX,
                        double cursorY,
                        GradingSettings& settings,
                        bool doubleClick,
                        bool* saveRequested,
                        bool* previewToggleRequested)
{
    const double relX = cursorX - static_cast<double>(layout.offset.x);
    const double relY = cursorY - static_cast<double>(layout.offset.y);
    if (relX < 0.0 || relY < 0.0 || relX >= layout.width || relY >= layout.height)
    {
        return false;
    }

    // Reset button hit test
    if (relX >= layout.resetX0 && relX <= layout.resetX1 && relY >= layout.resetY0 && relY <= layout.resetY1)
    {
        setGradingDefaults(settings);
        return true;
    }
    // Save button hit test
    if (relX >= layout.saveX0 && relX <= layout.saveX1 && relY >= layout.saveY0 && relY <= layout.saveY1)
    {
        if (saveRequested)
        {
            *saveRequested = true;
        }
        return true;
    }
    // Preview button hit test
    if (relX >= layout.previewX0 && relX <= layout.previewX1 && relY >= layout.previewY0 && relY <= layout.previewY1)
    {
        if (previewToggleRequested)
        {
            *previewToggleRequested = true;
        }
        return true;
    }

    const uint32_t padding = 12;
    const uint32_t barWidth = layout.width - padding * 2;
    const uint32_t barYStart = layout.barYStart;
    const uint32_t rowHeight = layout.barHeight + layout.rowSpacing;
    int sliderIndex = static_cast<int>((relY - barYStart) / rowHeight);
    if (sliderIndex < 0 || sliderIndex >= 12)
    {
        return false;
    }

    float t = clamp01(static_cast<float>((relX - padding) / static_cast<double>(barWidth)));
    switch (sliderIndex)
    {
    case 0:
        settings.exposure = doubleClick ? 0.0f : denormalizeExposure(t);
        break;
    case 1:
        settings.contrast = doubleClick ? 1.0f : denormalizeContrast(t);
        break;
    case 2:
        settings.saturation = doubleClick ? 1.0f : denormalizeSaturation(t);
        break;
    case 3:
        settings.shadows.r = doubleClick ? 1.0f : std::clamp(t * 2.0f, 0.0f, 2.0f);
        break;
    case 4:
        settings.shadows.g = doubleClick ? 1.0f : std::clamp(t * 2.0f, 0.0f, 2.0f);
        break;
    case 5:
        settings.shadows.b = doubleClick ? 1.0f : std::clamp(t * 2.0f, 0.0f, 2.0f);
        break;
    case 6:
        settings.midtones.r = doubleClick ? 1.0f : std::clamp(t * 2.0f, 0.0f, 2.0f);
        break;
    case 7:
        settings.midtones.g = doubleClick ? 1.0f : std::clamp(t * 2.0f, 0.0f, 2.0f);
        break;
    case 8:
        settings.midtones.b = doubleClick ? 1.0f : std::clamp(t * 2.0f, 0.0f, 2.0f);
        break;
    case 9:
        settings.highlights.r = doubleClick ? 1.0f : std::clamp(t * 2.0f, 0.0f, 2.0f);
        break;
    case 10:
        settings.highlights.g = doubleClick ? 1.0f : std::clamp(t * 2.0f, 0.0f, 2.0f);
        break;
    case 11:
        settings.highlights.b = doubleClick ? 1.0f : std::clamp(t * 2.0f, 0.0f, 2.0f);
        break;
    default:
        break;
    }
    return true;
}

void setGradingDefaults(GradingSettings& settings)
{
    settings.exposure = 0.0f;
    settings.contrast = 1.0f;
    settings.saturation = 1.0f;
    settings.shadows = glm::vec3(1.0f);
    settings.midtones = glm::vec3(1.0f);
    settings.highlights = glm::vec3(1.0f);
}

namespace
{
bool parseFloat(const std::string& src, const std::string& key, float& outValue)
{
    const std::string needle = "\"" + key + "\"";
    size_t pos = src.find(needle);
    if (pos == std::string::npos)
    {
        return false;
    }
    pos = src.find(':', pos);
    if (pos == std::string::npos)
    {
        return false;
    }
    const char* start = src.c_str() + pos + 1;
    char* endPtr = nullptr;
    float v = std::strtof(start, &endPtr);
    if (endPtr == start)
    {
        return false;
    }
    outValue = v;
    return true;
}

glm::vec3 parseVec3(const std::string& src, const std::string& key, const glm::vec3& fallback)
{
    const std::string needle = "\"" + key + "\"";
    size_t pos = src.find(needle);
    if (pos == std::string::npos)
    {
        return fallback;
    }
    pos = src.find('[', pos);
    if (pos == std::string::npos)
    {
        return fallback;
    }
    glm::vec3 result = fallback;
    const char* cursor = src.c_str() + pos + 1;
    for (int i = 0; i < 3; ++i)
    {
        char* endPtr = nullptr;
        float v = std::strtof(cursor, &endPtr);
        if (endPtr == cursor)
        {
            return fallback;
        }
        result[i] = v;
        cursor = endPtr;
        while (*cursor != '\0' && *cursor != ',' && *cursor != ']')
        {
            ++cursor;
        }
        if (i < 2)
        {
            if (*cursor != ',')
            {
                return fallback;
            }
            ++cursor;
        }
    }
    return result;
}
} // namespace

bool loadGradingSettings(const std::filesystem::path& path, GradingSettings& settings)
{
    setGradingDefaults(settings);
    if (!std::filesystem::exists(path))
    {
        return false;
    }

    std::ifstream in(path);
    if (!in.is_open())
    {
        return false;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string contents = buffer.str();

    parseFloat(contents, "exposure", settings.exposure);
    parseFloat(contents, "contrast", settings.contrast);
    parseFloat(contents, "saturation", settings.saturation);
    settings.shadows = parseVec3(contents, "shadows", settings.shadows);
    settings.midtones = parseVec3(contents, "midtones", settings.midtones);
    settings.highlights = parseVec3(contents, "highlights", settings.highlights);
    return true;
}

bool saveGradingSettings(const std::filesystem::path& path, const GradingSettings& settings)
{
    std::ofstream out(path);
    if (!out.is_open())
    {
        return false;
    }
    out << "{\n";
    out << "  \"exposure\": " << settings.exposure << ",\n";
    out << "  \"contrast\": " << settings.contrast << ",\n";
    out << "  \"saturation\": " << settings.saturation << ",\n";
    out << "  \"shadows\": [" << settings.shadows.r << ", " << settings.shadows.g << ", " << settings.shadows.b << "],\n";
    out << "  \"midtones\": [" << settings.midtones.r << ", " << settings.midtones.g << ", " << settings.midtones.b << "],\n";
    out << "  \"highlights\": [" << settings.highlights.r << ", " << settings.highlights.g << ", " << settings.highlights.b << "]\n";
    out << "}\n";
    return true;
}
} // namespace grading
