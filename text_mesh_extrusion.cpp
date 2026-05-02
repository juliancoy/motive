#include "text_mesh_extrusion.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <vector>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

#include "external/mapbox/earcut.hpp"
#include "primitive.h"

namespace motive::text {
namespace {

using Point2 = std::array<double, 2>;

struct Contour2D
{
    std::vector<glm::vec2> points;
    double signedArea = 0.0;
};

struct OutlineBuildState
{
    std::vector<Contour2D> contours;
    std::vector<glm::vec2> current;
    glm::vec2 pen = glm::vec2(0.0f);
    glm::vec2 minPt = glm::vec2(std::numeric_limits<float>::max());
    glm::vec2 maxPt = glm::vec2(std::numeric_limits<float>::lowest());
    float flatnessTolerancePx = 0.75f;
};

inline glm::vec2 ftToVec2(const FT_Vector* v, const glm::vec2& pen)
{
    return glm::vec2(static_cast<float>(v->x) / 64.0f + pen.x,
                     static_cast<float>(v->y) / 64.0f + pen.y);
}

void updateBounds(OutlineBuildState& st, const glm::vec2& p)
{
    st.minPt = glm::min(st.minPt, p);
    st.maxPt = glm::max(st.maxPt, p);
}

double signedArea2D(const std::vector<glm::vec2>& poly)
{
    if (poly.size() < 3)
    {
        return 0.0;
    }
    double sum = 0.0;
    for (size_t i = 0; i < poly.size(); ++i)
    {
        const glm::vec2& a = poly[i];
        const glm::vec2& b = poly[(i + 1) % poly.size()];
        sum += static_cast<double>(a.x) * static_cast<double>(b.y) -
               static_cast<double>(b.x) * static_cast<double>(a.y);
    }
    return 0.5 * sum;
}

void finalizeCurrentContour(OutlineBuildState& st)
{
    if (st.current.size() < 3)
    {
        st.current.clear();
        return;
    }

    if (glm::length(st.current.front() - st.current.back()) <= 1e-4f)
    {
        st.current.pop_back();
    }
    if (st.current.size() < 3)
    {
        st.current.clear();
        return;
    }

    Contour2D contour;
    contour.points = std::move(st.current);
    contour.signedArea = signedArea2D(contour.points);
    if (std::abs(contour.signedArea) > 1e-6)
    {
        st.contours.push_back(std::move(contour));
    }
    st.current.clear();
}

int moveToCb(const FT_Vector* to, void* user)
{
    auto* st = static_cast<OutlineBuildState*>(user);
    finalizeCurrentContour(*st);
    const glm::vec2 p = ftToVec2(to, st->pen);
    st->current.push_back(p);
    updateBounds(*st, p);
    return 0;
}

int lineToCb(const FT_Vector* to, void* user)
{
    auto* st = static_cast<OutlineBuildState*>(user);
    const glm::vec2 p = ftToVec2(to, st->pen);
    st->current.push_back(p);
    updateBounds(*st, p);
    return 0;
}

int conicToCb(const FT_Vector* control, const FT_Vector* to, void* user)
{
    auto* st = static_cast<OutlineBuildState*>(user);
    if (st->current.empty())
    {
        return 0;
    }

    const glm::vec2 p0 = st->current.back();
    const glm::vec2 p1 = ftToVec2(control, st->pen);
    const glm::vec2 p2 = ftToVec2(to, st->pen);

    const float chord = glm::length(p2 - p0);
    const float sag = glm::length(p1 - (p0 + p2) * 0.5f);
    const float curvature = std::max(0.0f, sag - chord * 0.25f);
    const float tol = std::max(0.15f, st->flatnessTolerancePx);
    const int steps = std::clamp(static_cast<int>(std::ceil(std::max(chord * 0.10f, curvature) / tol)), 8, 128);
    for (int i = 1; i <= steps; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const float u = 1.0f - t;
        const glm::vec2 p = u * u * p0 + 2.0f * u * t * p1 + t * t * p2;
        st->current.push_back(p);
        updateBounds(*st, p);
    }
    return 0;
}

int cubicToCb(const FT_Vector* c1, const FT_Vector* c2, const FT_Vector* to, void* user)
{
    auto* st = static_cast<OutlineBuildState*>(user);
    if (st->current.empty())
    {
        return 0;
    }

    const glm::vec2 p0 = st->current.back();
    const glm::vec2 p1 = ftToVec2(c1, st->pen);
    const glm::vec2 p2 = ftToVec2(c2, st->pen);
    const glm::vec2 p3 = ftToVec2(to, st->pen);

    const float chord = glm::length(p3 - p0);
    const float dev1 = glm::length(p1 - (p0 + p3) * 0.5f);
    const float dev2 = glm::length(p2 - (p0 + p3) * 0.5f);
    const float curvature = std::max(dev1, dev2);
    const float tol = std::max(0.15f, st->flatnessTolerancePx);
    const int steps = std::clamp(static_cast<int>(std::ceil(std::max(chord * 0.14f, curvature) / tol)), 10, 160);
    for (int i = 1; i <= steps; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const float u = 1.0f - t;
        const glm::vec2 p =
            u * u * u * p0 +
            3.0f * u * u * t * p1 +
            3.0f * u * t * t * p2 +
            t * t * t * p3;
        st->current.push_back(p);
        updateBounds(*st, p);
    }
    return 0;
}

bool pointInPoly(const glm::vec2& p, const std::vector<glm::vec2>& poly)
{
    bool inside = false;
    size_t j = poly.size() - 1;
    for (size_t i = 0; i < poly.size(); j = i++)
    {
        const glm::vec2& pi = poly[i];
        const glm::vec2& pj = poly[j];
        const bool cross = ((pi.y > p.y) != (pj.y > p.y));
        if (!cross)
        {
            continue;
        }
        const float xCut = (pj.x - pi.x) * (p.y - pi.y) / (pj.y - pi.y + 1e-12f) + pi.x;
        if (p.x < xCut)
        {
            inside = !inside;
        }
    }
    return inside;
}

glm::vec2 contourCentroid(const std::vector<glm::vec2>& poly)
{
    glm::vec2 c(0.0f);
    for (const glm::vec2& p : poly)
    {
        c += p;
    }
    if (!poly.empty())
    {
        c /= static_cast<float>(poly.size());
    }
    return c;
}

void appendFace(std::vector<Vertex>& out,
                const glm::vec3& a,
                const glm::vec3& b,
                const glm::vec3& c,
                const glm::vec3& d,
                const glm::vec3& normal,
                const glm::vec2& uv)
{
    out.push_back(Vertex{a, normal, uv});
    out.push_back(Vertex{b, normal, uv});
    out.push_back(Vertex{c, normal, uv});
    out.push_back(Vertex{a, normal, uv});
    out.push_back(Vertex{c, normal, uv});
    out.push_back(Vertex{d, normal, uv});
}

void appendFaceChecked(std::vector<Vertex>& out,
                       const glm::vec3& a,
                       const glm::vec3& b,
                       const glm::vec3& c,
                       const glm::vec3& d,
                       const glm::vec3& normal,
                       const glm::vec2& uv)
{
    const glm::vec3 faceCross = glm::cross(b - a, c - a);
    if (glm::dot(faceCross, normal) >= 0.0f)
    {
        appendFace(out, a, b, c, d, normal, uv);
    }
    else
    {
        appendFace(out, b, a, d, c, normal, uv);
    }
}

void appendFace(std::vector<Vertex>& out,
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
    out.push_back(Vertex{a, normal, uva});
    out.push_back(Vertex{b, normal, uvb});
    out.push_back(Vertex{c, normal, uvc});
    out.push_back(Vertex{a, normal, uva});
    out.push_back(Vertex{c, normal, uvc});
    out.push_back(Vertex{d, normal, uvd});
}

std::vector<Contour2D> buildGlyphContours(const std::string& text,
                                          uint32_t pixelHeight,
                                          const FontRenderOptions& options,
                                          glm::vec2* outMin,
                                          glm::vec2* outMax)
{
    FT_Library library = nullptr;
    if (FT_Init_FreeType(&library) != 0)
    {
        return {};
    }

    std::string fontPath = options.fontPath.empty() ? "nofile.ttf" : options.fontPath;
    FT_Face face = nullptr;
    if (FT_New_Face(library, fontPath.c_str(), 0, &face) != 0)
    {
        FT_Done_FreeType(library);
        return {};
    }

    FT_Set_Pixel_Sizes(face, 0, pixelHeight);

    OutlineBuildState state;
    const float px = static_cast<float>(std::max<uint32_t>(pixelHeight, 8u));
    state.flatnessTolerancePx = std::clamp(96.0f / px, 0.20f, 0.60f);

    FT_UInt prev = 0;
    const bool hasKerning = FT_HAS_KERNING(face);
    FT_Outline_Funcs funcs{};
    funcs.move_to = moveToCb;
    funcs.line_to = lineToCb;
    funcs.conic_to = conicToCb;
    funcs.cubic_to = cubicToCb;
    funcs.shift = 0;
    funcs.delta = 0;

    for (char ch : text)
    {
        const FT_UInt glyph = FT_Get_Char_Index(face, static_cast<FT_ULong>(static_cast<unsigned char>(ch)));
        if (hasKerning && prev && glyph)
        {
            FT_Vector k{};
            if (FT_Get_Kerning(face, prev, glyph, FT_KERNING_DEFAULT, &k) == 0)
            {
                state.pen.x += static_cast<float>(k.x) / 64.0f;
            }
        }

        if (FT_Load_Glyph(face, glyph, FT_LOAD_NO_BITMAP) != 0)
        {
            prev = glyph;
            continue;
        }
        if (face->glyph->format != FT_GLYPH_FORMAT_OUTLINE)
        {
            prev = glyph;
            state.pen.x += static_cast<float>(face->glyph->advance.x) / 64.0f + static_cast<float>(options.letterSpacing);
            continue;
        }

        FT_Outline_Decompose(&face->glyph->outline, &funcs, &state);
        finalizeCurrentContour(state);

        state.pen.x += static_cast<float>(face->glyph->advance.x) / 64.0f + static_cast<float>(options.letterSpacing);
        prev = glyph;
    }

    FT_Done_Face(face);
    FT_Done_FreeType(library);

    if (outMin)
    {
        *outMin = state.minPt;
    }
    if (outMax)
    {
        *outMax = state.maxPt;
    }

    return state.contours;
}

}  // namespace

std::vector<Vertex> buildExtrudedTextVerticesFromText(const std::string& text,
                                                      uint32_t pixelHeight,
                                                      const FontRenderOptions& options,
                                                      float depth,
                                                      float bevelScale)
{
    std::vector<Vertex> vertices;
    glm::vec2 minPt;
    glm::vec2 maxPt;
    std::vector<Contour2D> contours = buildGlyphContours(text, pixelHeight, options, &minPt, &maxPt);
    if (contours.empty())
    {
        return vertices;
    }

    const float w = std::max(1e-3f, maxPt.x - minPt.x);
    const float h = std::max(1e-3f, maxPt.y - minPt.y);
    const float scale = 1.0f / h;
    const float halfD = std::max(0.0f, depth) * 0.5f;
    const float widthNorm = std::max(1e-3f, w * scale);
    const float clampedBevelScale = std::clamp(bevelScale, 0.0f, 2.0f);
    const float bevelInset = std::min(0.03f, std::max(0.0f, depth * 0.22f * clampedBevelScale));
    const float bevelZ = std::min(halfD * 0.85f, std::max(0.001f, bevelInset * 0.75f));

    std::vector<Contour2D> normContours = contours;
    for (Contour2D& c : normContours)
    {
        for (glm::vec2& p : c.points)
        {
            p.x = (p.x - minPt.x) * scale - (w * scale * 0.5f);
            p.y = (p.y - minPt.y) * scale - 0.5f;
        }
        c.signedArea = signedArea2D(c.points);
    }

    size_t seedOuter = 0;
    double seedAbs = 0.0;
    for (size_t i = 0; i < normContours.size(); ++i)
    {
        const double a = std::abs(normContours[i].signedArea);
        if (a > seedAbs)
        {
            seedAbs = a;
            seedOuter = i;
        }
    }
    const bool outerIsCcw = normContours[seedOuter].signedArea > 0.0;

    std::vector<size_t> outerIdx;
    std::vector<size_t> holeIdx;
    for (size_t i = 0; i < normContours.size(); ++i)
    {
        const bool ccw = normContours[i].signedArea > 0.0;
        if (ccw == outerIsCcw)
        {
            outerIdx.push_back(i);
        }
        else
        {
            holeIdx.push_back(i);
        }
    }

    std::vector<std::vector<size_t>> outerToHoles(outerIdx.size());
    for (size_t hi : holeIdx)
    {
        const glm::vec2 c = contourCentroid(normContours[hi].points);
        for (size_t oi = 0; oi < outerIdx.size(); ++oi)
        {
            if (pointInPoly(c, normContours[outerIdx[oi]].points))
            {
                outerToHoles[oi].push_back(hi);
                break;
            }
        }
    }

    const glm::vec2 uvMid(0.5f, 0.5f);

    for (size_t oi = 0; oi < outerIdx.size(); ++oi)
    {
        std::vector<std::vector<Point2>> polygon;
        auto addRing = [&](const std::vector<glm::vec2>& ring, bool wantCcw)
        {
            std::vector<glm::vec2> tmp = ring;
            const bool ccw = signedArea2D(tmp) > 0.0;
            if (ccw != wantCcw)
            {
                std::reverse(tmp.begin(), tmp.end());
            }
            std::vector<Point2> pts;
            pts.reserve(tmp.size());
            for (const glm::vec2& p : tmp)
            {
                pts.push_back({static_cast<double>(p.x), static_cast<double>(p.y)});
            }
            polygon.push_back(std::move(pts));
        };

        addRing(normContours[outerIdx[oi]].points, true);
        for (size_t hi : outerToHoles[oi])
        {
            addRing(normContours[hi].points, false);
        }

        std::vector<uint32_t> idx = mapbox::earcut<uint32_t>(polygon);
        std::vector<glm::vec2> flat;
        for (const auto& ring : polygon)
        {
            for (const auto& p : ring)
            {
                flat.push_back(glm::vec2(static_cast<float>(p[0]), static_cast<float>(p[1])));
            }
        }

        for (size_t i = 0; i + 2 < idx.size(); i += 3)
        {
            const glm::vec2 p0 = flat[idx[i + 0]];
            const glm::vec2 p1 = flat[idx[i + 1]];
            const glm::vec2 p2 = flat[idx[i + 2]];

            const glm::vec2 uv0((p0.x + widthNorm * 0.5f) / widthNorm, 1.0f - (p0.y + 0.5f));
            const glm::vec2 uv1((p1.x + widthNorm * 0.5f) / widthNorm, 1.0f - (p1.y + 0.5f));
            const glm::vec2 uv2((p2.x + widthNorm * 0.5f) / widthNorm, 1.0f - (p2.y + 0.5f));

            vertices.push_back(Vertex{glm::vec3(p0, halfD), glm::vec3(0.0f, 0.0f, 1.0f), uv0});
            vertices.push_back(Vertex{glm::vec3(p1, halfD), glm::vec3(0.0f, 0.0f, 1.0f), uv1});
            vertices.push_back(Vertex{glm::vec3(p2, halfD), glm::vec3(0.0f, 0.0f, 1.0f), uv2});

            vertices.push_back(Vertex{glm::vec3(p2, -halfD), glm::vec3(0.0f, 0.0f, -1.0f), uvMid});
            vertices.push_back(Vertex{glm::vec3(p1, -halfD), glm::vec3(0.0f, 0.0f, -1.0f), uvMid});
            vertices.push_back(Vertex{glm::vec3(p0, -halfD), glm::vec3(0.0f, 0.0f, -1.0f), uvMid});
        }
    }

    for (const Contour2D& contour : normContours)
    {
        const bool ccw = contour.signedArea > 0.0;
        const size_t n = contour.points.size();
        auto emitContourQuad = [&](const glm::vec3& p0,
                                   const glm::vec3& p1,
                                   const glm::vec3& p2,
                                   const glm::vec3& p3,
                                   const glm::vec3& normal)
        {
            appendFaceChecked(vertices, p0, p1, p2, p3, normal, uvMid);
        };

        std::vector<glm::vec2> insetRing(n);
        for (size_t i = 0; i < n; ++i)
        {
            const glm::vec2 pPrev = contour.points[(i + n - 1) % n];
            const glm::vec2 pCurr = contour.points[i];
            const glm::vec2 pNext = contour.points[(i + 1) % n];
            glm::vec2 ePrev = pCurr - pPrev;
            glm::vec2 eNext = pNext - pCurr;
            const float lenPrev = glm::length(ePrev);
            const float lenNext = glm::length(eNext);
            if (lenPrev < 1e-6f || lenNext < 1e-6f)
            {
                insetRing[i] = pCurr;
                continue;
            }
            ePrev /= lenPrev;
            eNext /= lenNext;
            glm::vec2 nPrev(ePrev.y, -ePrev.x);
            glm::vec2 nNext(eNext.y, -eNext.x);
            if (!ccw)
            {
                nPrev = -nPrev;
                nNext = -nNext;
            }
            glm::vec2 nAvg = nPrev + nNext;
            if (glm::length(nAvg) < 1e-5f)
            {
                nAvg = nNext;
            }
            nAvg = glm::normalize(nAvg);
            const float cornerDot = std::clamp(glm::dot(nAvg, nNext), 0.35f, 1.0f);
            float miterScale = 1.0f / cornerDot;

            const float turn = ePrev.x * eNext.y - ePrev.y * eNext.x;
            const bool reflexCorner = ccw ? (turn < 0.0f) : (turn > 0.0f);
            if (reflexCorner)
            {
                nAvg = nNext;
                miterScale = 0.75f;
            }

            const float desiredOffset = bevelInset * miterScale;
            const float maxOffset = std::min(lenPrev, lenNext) * 0.45f;
            const float cornerOffset = std::min(desiredOffset, maxOffset);
            insetRing[i] = pCurr + nAvg * cornerOffset;
        }

        for (size_t i = 0; i < n; ++i)
        {
            const glm::vec2 a2 = contour.points[i];
            const glm::vec2 b2 = contour.points[(i + 1) % n];
            const glm::vec2 ai2 = insetRing[i];
            const glm::vec2 bi2 = insetRing[(i + 1) % n];
            const glm::vec2 e = b2 - a2;
            if (glm::length(e) < 1e-6f)
            {
                continue;
            }
            glm::vec3 normal = glm::normalize(glm::vec3(e.y, -e.x, 0.0f));
            if (!ccw)
            {
                normal = -normal;
            }

            const glm::vec3 aF(a2, halfD);
            const glm::vec3 bF(b2, halfD);
            const glm::vec3 aB(a2, -halfD);
            const glm::vec3 bB(b2, -halfD);
            const glm::vec3 aFi(ai2, halfD - bevelZ);
            const glm::vec3 bFi(bi2, halfD - bevelZ);
            const glm::vec3 aBi(ai2, -halfD + bevelZ);
            const glm::vec3 bBi(bi2, -halfD + bevelZ);
            const glm::vec3 frontBevelNormal = glm::normalize(glm::vec3(normal.x, normal.y, 0.7f));
            const glm::vec3 backBevelNormal = glm::normalize(glm::vec3(normal.x, normal.y, -0.7f));

            emitContourQuad(aF, bF, bFi, aFi, frontBevelNormal);
            emitContourQuad(aBi, bBi, bFi, aFi, normal);
            emitContourQuad(aB, bB, bBi, aBi, backBevelNormal);
        }
    }

    return vertices;
}

std::vector<Vertex> buildExtrudedTextVerticesFromText(const std::string& text,
                                                      const ExtrudedTextOptions& options)
{
    const uint32_t pixelHeight = std::max<uint32_t>(8u, options.pixelHeight);
    const uint32_t supersample = std::max<uint32_t>(1u, options.meshSupersample);
    return buildExtrudedTextVerticesFromText(text,
                                             pixelHeight * supersample,
                                             options.font,
                                             std::max(0.0f, options.depth),
                                             options.bevelScale);
}

void applyExtrudedTextMaterial(Model& model,
                               const glm::vec3& color,
                               bool depthTestEnabled,
                               bool depthWriteEnabled)
{
    for (auto& mesh : model.meshes)
    {
        for (auto& primitive : mesh.primitives)
        {
            if (!primitive)
            {
                continue;
            }
            primitive->alphaMode = PrimitiveAlphaMode::Opaque;
            primitive->cullMode = PrimitiveCullMode::Disabled;
            primitive->depthTestEnabled = depthTestEnabled;
            primitive->depthWriteEnabled = depthWriteEnabled;
            primitive->forceAlphaOne = false;
        }
    }
    model.setPaintOverride(true, color);
}

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

            appendFace(vertices, fbl, fbr, ftr, ftl, glm::vec3(0.0f, 0.0f, 1.0f), uva, uvb, uvc, uvd);

            appendFace(vertices, bbr, bbl, btl, btr, glm::vec3(0.0f, 0.0f, -1.0f), solidUv, solidUv, solidUv, solidUv);

            if (!opaque(x - 1, y))
            {
                appendFace(vertices, bbl, fbl, ftl, btl, glm::vec3(-1.0f, 0.0f, 0.0f), solidUv, solidUv, solidUv, solidUv);
            }
            if (!opaque(x + 1, y))
            {
                appendFace(vertices, fbr, bbr, btr, ftr, glm::vec3(1.0f, 0.0f, 0.0f), solidUv, solidUv, solidUv, solidUv);
            }
            if (!opaque(x, y - 1))
            {
                appendFace(vertices, ftl, ftr, btr, btl, glm::vec3(0.0f, 1.0f, 0.0f), solidUv, solidUv, solidUv, solidUv);
            }
            if (!opaque(x, y + 1))
            {
                appendFace(vertices, bbl, bbr, fbr, fbl, glm::vec3(0.0f, -1.0f, 0.0f), solidUv, solidUv, solidUv, solidUv);
            }
        }
    }
    return vertices;
}

}  // namespace motive::text
