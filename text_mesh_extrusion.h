#pragma once

#include "model.h"
#include "text_rendering.h"

#include <vector>

namespace motive::text {

std::vector<Vertex> buildExtrudedTextVertices(const OverlayBitmap& bitmap, float depth);

}  // namespace motive::text
