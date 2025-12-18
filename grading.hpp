#pragma once

#include <cstdint>
#include <vector>
#include <filesystem>
#include <glm/vec3.hpp>

#include "display2d.h"
#include "overlay.hpp"

class Engine;

struct GradingSettings
{
    float exposure = 0.0f;   // stops
    float contrast = 1.0f;   // multiplier
    float saturation = 1.0f; // multiplier
    glm::vec3 shadows = glm::vec3(1.0f);
    glm::vec3 midtones = glm::vec3(1.0f);
    glm::vec3 highlights = glm::vec3(1.0f);
};

namespace grading
{

struct SliderLayout
{
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t margin = 20;
    uint32_t barHeight = 20;
    uint32_t barYStart = 32;
    uint32_t rowSpacing = 12;
    uint32_t handleRadius = 8;
    VkOffset2D offset{0, 0};
    std::vector<std::string> labels;
    // Reset button bounds (overlay-local coords)
    uint32_t resetX0 = 0;
    uint32_t resetY0 = 0;
    uint32_t resetX1 = 0;
    uint32_t resetY1 = 0;
    uint32_t resetWidth = 140;
    uint32_t resetHeight = 36;
    uint32_t saveX0 = 0;
    uint32_t saveY0 = 0;
    uint32_t saveX1 = 0;
    uint32_t saveY1 = 0;
    uint32_t saveWidth = 140;
    uint32_t saveHeight = 36;
    uint32_t previewX0 = 0;
    uint32_t previewY0 = 0;
    uint32_t previewX1 = 0;
    uint32_t previewY1 = 0;
    uint32_t previewWidth = 160;
    uint32_t previewHeight = 36;
};

// Build/update the grading overlay texture based on current settings; places it centered at bottom.
bool buildGradingOverlay(Engine* engine,
                         const GradingSettings& settings,
                         overlay::ImageResource& image,
                         OverlayImageInfo& info,
                         uint32_t fbWidth,
                         uint32_t fbHeight,
                         SliderLayout& layout,
                         bool previewEnabled);

// Map a click within the overlay to one of the slider values. Returns true if a value changed.
bool handleOverlayClick(const SliderLayout& layout,
                        double cursorX,
                        double cursorY,
                        GradingSettings& settings,
                        bool doubleClick = false,
                        bool* saveRequested = nullptr,
                        bool* previewToggleRequested = nullptr);

void setGradingDefaults(GradingSettings& settings);
bool loadGradingSettings(const std::filesystem::path& path, GradingSettings& settings);
bool saveGradingSettings(const std::filesystem::path& path, const GradingSettings& settings);

} // namespace grading
