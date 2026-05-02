#pragma once

#include "model.h"
#include "text_rendering.h"

#include <cstdint>
#include <string>
#include <vector>

namespace motive::text {

constexpr uint32_t kDefaultExtrudedTextMeshSupersample = 3;
constexpr float kDefaultExtrudedTextBevelScale = 1.0f;

struct ExtrudedTextOptions
{
    uint32_t pixelHeight = 132;
    uint32_t meshSupersample = kDefaultExtrudedTextMeshSupersample;
    FontRenderOptions font;
    float depth = 0.22f;
    float bevelScale = kDefaultExtrudedTextBevelScale;
};

std::vector<Vertex> buildExtrudedTextVertices(const OverlayBitmap& bitmap, float depth);
std::vector<Vertex> buildExtrudedTextVerticesFromText(const std::string& text,
                                                      uint32_t pixelHeight,
                                                      const FontRenderOptions& options,
                                                      float depth,
                                                      float bevelScale = 1.0f);
std::vector<Vertex> buildExtrudedTextVerticesFromText(const std::string& text,
                                                      const ExtrudedTextOptions& options);
void applyExtrudedTextMaterial(Model& model,
                               const glm::vec3& color,
                               bool depthTestEnabled,
                               bool depthWriteEnabled);

}  // namespace motive::text
