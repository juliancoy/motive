#include "text_mesh_extrusion.h"

#include <algorithm>
#include <cstddef>

namespace motive::text {
namespace {

void appendTextFace(std::vector<Vertex>& vertices,
                    const glm::vec3& a,
                    const glm::vec3& b,
                    const glm::vec3& c,
                    const glm::vec3& d,
                    const glm::vec3& normal,
                    const glm::vec2& uva,
                    const glm::vec2& uvb,
                    const glm::vec2& uvc,
                    const glm::vec2& uvd)
{
    const glm::vec3 faceCross = glm::cross(b - a, c - a);
    const bool windingMatchesNormal = glm::dot(faceCross, normal) >= 0.0f;

    if (windingMatchesNormal)
    {
        vertices.push_back(Vertex{a, normal, uva});
        vertices.push_back(Vertex{b, normal, uvb});
        vertices.push_back(Vertex{c, normal, uvc});
        vertices.push_back(Vertex{a, normal, uva});
        vertices.push_back(Vertex{c, normal, uvc});
        vertices.push_back(Vertex{d, normal, uvd});
        return;
    }

    vertices.push_back(Vertex{a, normal, uva});
    vertices.push_back(Vertex{c, normal, uvc});
    vertices.push_back(Vertex{b, normal, uvb});
    vertices.push_back(Vertex{a, normal, uva});
    vertices.push_back(Vertex{d, normal, uvd});
    vertices.push_back(Vertex{c, normal, uvc});
}

}  // namespace

std::vector<Vertex> buildExtrudedTextVertices(const OverlayBitmap& bitmap, float depth)
{
    std::vector<Vertex> vertices;
    if (bitmap.width == 0 || bitmap.height == 0 || bitmap.pixels.empty())
    {
        return vertices;
    }
    const uint32_t width = bitmap.width;
    const uint32_t height = bitmap.height;
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    const float halfW = 0.5f * aspect;
    const float halfH = 0.5f;
    const float halfD = std::max(0.0f, depth) * 0.5f;
    const auto opaque = [&](int x, int y) -> bool {
        if (x < 0 || y < 0 || x >= static_cast<int>(width) || y >= static_cast<int>(height))
        {
            return false;
        }
        const size_t idx = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4u + 3u;
        return idx < bitmap.pixels.size() && bitmap.pixels[idx] > 8u;
    };
    glm::vec2 solidUv(0.5f, 0.5f);
    bool foundSolidUv = false;
    for (int y = 0; y < static_cast<int>(height) && !foundSolidUv; ++y)
    {
        for (int x = 0; x < static_cast<int>(width); ++x)
        {
            if (opaque(x, y))
            {
                const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(width);
                const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(height);
                solidUv = glm::vec2(u, v);
                foundSolidUv = true;
                break;
            }
        }
    }

    vertices.reserve(static_cast<size_t>(width) * static_cast<size_t>(height));
    for (int y = 0; y < static_cast<int>(height); ++y)
    {
        for (int x = 0; x < static_cast<int>(width); ++x)
        {
            if (!opaque(x, y))
            {
                continue;
            }
            const float x0n = static_cast<float>(x) / static_cast<float>(width);
            const float x1n = static_cast<float>(x + 1) / static_cast<float>(width);
            const float y0n = static_cast<float>(y) / static_cast<float>(height);
            const float y1n = static_cast<float>(y + 1) / static_cast<float>(height);
            const float px0 = -halfW + x0n * aspect;
            const float px1 = -halfW + x1n * aspect;
            const float py0 = halfH - y0n;
            const float py1 = halfH - y1n;
            const glm::vec2 uva(x0n, y1n);
            const glm::vec2 uvb(x1n, y1n);
            const glm::vec2 uvc(x1n, y0n);
            const glm::vec2 uvd(x0n, y0n);

            const glm::vec3 fbl(px0, py1, halfD);
            const glm::vec3 fbr(px1, py1, halfD);
            const glm::vec3 ftr(px1, py0, halfD);
            const glm::vec3 ftl(px0, py0, halfD);
            const glm::vec3 bbl(px0, py1, -halfD);
            const glm::vec3 bbr(px1, py1, -halfD);
            const glm::vec3 btr(px1, py0, -halfD);
            const glm::vec3 btl(px0, py0, -halfD);

            appendTextFace(vertices, fbl, fbr, ftr, ftl, glm::vec3(0.0f, 0.0f, 1.0f), uva, uvb, uvc, uvd);

            appendTextFace(vertices, bbr, bbl, btl, btr, glm::vec3(0.0f, 0.0f, -1.0f), solidUv, solidUv, solidUv, solidUv);

            if (!opaque(x - 1, y))
            {
                appendTextFace(vertices, bbl, fbl, ftl, btl, glm::vec3(-1.0f, 0.0f, 0.0f), solidUv, solidUv, solidUv, solidUv);
            }
            if (!opaque(x + 1, y))
            {
                appendTextFace(vertices, fbr, bbr, btr, ftr, glm::vec3(1.0f, 0.0f, 0.0f), solidUv, solidUv, solidUv, solidUv);
            }
            if (!opaque(x, y - 1))
            {
                appendTextFace(vertices, ftl, ftr, btr, btl, glm::vec3(0.0f, 1.0f, 0.0f), solidUv, solidUv, solidUv, solidUv);
            }
            if (!opaque(x, y + 1))
            {
                appendTextFace(vertices, bbl, bbr, fbr, fbl, glm::vec3(0.0f, -1.0f, 0.0f), solidUv, solidUv, solidUv, solidUv);
            }
        }
    }
    return vertices;
}

}  // namespace motive::text
