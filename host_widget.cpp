
#include "camera.h"  // Include for Camera class and FollowOrbit
#include "asset_loader.h"
#include "camera_controller.h"
#include "hierarchy_builder.h"
#include "input_router.h"
#include "orbit_follow_rig.h"
#include "viewport_internal_utils.h"
#include "viewport_runtime.h"
#include "scene_controller.h"
#include "display.h"
#include "engine.h"
#include "light.h"
#include "model.h"
#include "object_transform.h"

#include <QDebug>
#include <QDragEnterEvent>
#include <QComboBox>
#include <QGridLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QUuid>
#include <QFrame>
#include <QDropEvent>
#include <QFocusEvent>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QMetaObject>
#include <QMimeData>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <chrono>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>

namespace motive::ui {

namespace {

bool couplingRequiresPhysics(const Model& model)
{
    return model.getAnimationPhysicsCoupling() != "AnimationOnly";
}

bool couplingUsesKinematicBody(const Model& model)
{
    return !model.character.isControllable && model.getAnimationPhysicsCoupling() == "Kinematic";
}

QJsonObject primitiveOverrideObject(int meshIndex, int primitiveIndex, const QString& cullMode, bool forceAlphaOne)
{
    return QJsonObject{
        {QStringLiteral("meshIndex"), meshIndex},
        {QStringLiteral("primitiveIndex"), primitiveIndex},
        {QStringLiteral("cullMode"), cullMode},
        {QStringLiteral("forceAlphaOne"), forceAlphaOne}
    };
}

void removePrimitiveOverrideIfDefault(QJsonArray& overrides, int meshIndex, int primitiveIndex, const QString& cullMode, bool forceAlphaOne)
{
    const bool isDefault = (cullMode == QStringLiteral("back")) && !forceAlphaOne;
    for (int i = overrides.size() - 1; i >= 0; --i)
    {
        if (!overrides.at(i).isObject())
        {
            continue;
        }

        const QJsonObject existing = overrides.at(i).toObject();
        if (existing.value(QStringLiteral("meshIndex")).toInt(-1) != meshIndex ||
            existing.value(QStringLiteral("primitiveIndex")).toInt(-1) != primitiveIndex)
        {
            continue;
        }

        if (isDefault)
        {
            overrides.removeAt(i);
        }
        else
        {
            overrides[i] = primitiveOverrideObject(meshIndex, primitiveIndex, cullMode, forceAlphaOne);
        }
        return;
    }

    if (!isDefault)
    {
        overrides.push_back(primitiveOverrideObject(meshIndex, primitiveIndex, cullMode, forceAlphaOne));
    }
}

glm::vec3 followAnchorPosition(const Model& model, const glm::vec3& targetOffset = glm::vec3(0.0f))
{
    return model.getFollowAnchorPosition() + targetOffset;
}

Model* modelForSceneIndex(Engine* engine, int sceneIndex)
{
    if (!engine || sceneIndex < 0 || sceneIndex >= static_cast<int>(engine->models.size()))
    {
        return nullptr;
    }
    auto& model = engine->models[static_cast<size_t>(sceneIndex)];
    return model ? model.get() : nullptr;
}

Model* firstControllableModel(Engine* engine)
{
    if (!engine)
    {
        return nullptr;
    }
    for (auto& model : engine->models)
    {
        if (model && model->character.isControllable)
        {
            return model.get();
        }
    }
    return nullptr;
}

bool pathLooksLikeNathanCharacter(const QString& sourcePath)
{
    const QString p = sourcePath.toLower();
    return p.contains(QStringLiteral("rp_nathan")) ||
           p.contains(QStringLiteral("nathan_animated")) ||
           p.contains(QStringLiteral("55-rp_nathan"));
}

bool pathLooksLikeEnvironmentScene(const QString& sourcePath)
{
    const QString p = sourcePath.toLower();
    return p.contains(QStringLiteral("/city/")) ||
           p.contains(QStringLiteral("china")) ||
           p.contains(QStringLiteral("scene.gltf"));
}

bool barycentricInTriangleXZ(const glm::vec3& a,
                             const glm::vec3& b,
                             const glm::vec3& c,
                             const glm::vec2& p,
                             float& u,
                             float& v,
                             float& w)
{
    const glm::vec2 a2(a.x, a.z);
    const glm::vec2 b2(b.x, b.z);
    const glm::vec2 c2(c.x, c.z);
    const glm::vec2 v0 = b2 - a2;
    const glm::vec2 v1 = c2 - a2;
    const glm::vec2 v2 = p - a2;
    const float d00 = glm::dot(v0, v0);
    const float d01 = glm::dot(v0, v1);
    const float d11 = glm::dot(v1, v1);
    const float d20 = glm::dot(v2, v0);
    const float d21 = glm::dot(v2, v1);
    const float denom = d00 * d11 - d01 * d01;
    if (std::abs(denom) < 1e-7f)
    {
        return false;
    }

    v = (d11 * d20 - d01 * d21) / denom;
    w = (d00 * d21 - d01 * d20) / denom;
    u = 1.0f - v - w;
    constexpr float kEpsilon = -0.0005f;
    return u >= kEpsilon && v >= kEpsilon && w >= kEpsilon;
}

struct CollisionTriangle
{
    glm::vec3 a = glm::vec3(0.0f);
    glm::vec3 b = glm::vec3(0.0f);
    glm::vec3 c = glm::vec3(0.0f);
    glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec2 minXZ = glm::vec2(0.0f);
    glm::vec2 maxXZ = glm::vec2(0.0f);
    float minY = 0.0f;
    float maxY = 0.0f;
};

struct StaticCollisionCache
{
    glm::mat4 worldTransform = glm::mat4(1.0f);
    std::vector<CollisionTriangle> groundTriangles;
    std::vector<CollisionTriangle> wallTriangles;
    std::unordered_map<std::int64_t, std::vector<size_t>> groundGrid;
    std::unordered_map<std::int64_t, std::vector<size_t>> wallGrid;
    bool valid = false;
};

std::unordered_map<const Model*, StaticCollisionCache> g_staticCollisionCaches;

bool matricesApproximatelyEqual(const glm::mat4& a, const glm::mat4& b)
{
    for (int col = 0; col < 4; ++col)
    {
        for (int row = 0; row < 4; ++row)
        {
            if (std::abs(a[col][row] - b[col][row]) > 1e-5f)
            {
                return false;
            }
        }
    }
    return true;
}

int collisionCellFor(float value)
{
    constexpr float kCellSize = 1.0f;
    return static_cast<int>(std::floor(value / kCellSize));
}

std::int64_t collisionCellKey(int x, int z)
{
    const std::uint64_t ux = static_cast<std::uint32_t>(x);
    const std::uint64_t uz = static_cast<std::uint32_t>(z);
    return static_cast<std::int64_t>((ux << 32u) | uz);
}

void insertTriangleIntoGrid(std::unordered_map<std::int64_t, std::vector<size_t>>& grid,
                            const CollisionTriangle& triangle,
                            size_t triangleIndex,
                            float expansion = 0.0f)
{
    const int minX = collisionCellFor(triangle.minXZ.x - expansion);
    const int maxX = collisionCellFor(triangle.maxXZ.x + expansion);
    const int minZ = collisionCellFor(triangle.minXZ.y - expansion);
    const int maxZ = collisionCellFor(triangle.maxXZ.y + expansion);
    for (int x = minX; x <= maxX; ++x)
    {
        for (int z = minZ; z <= maxZ; ++z)
        {
            grid[collisionCellKey(x, z)].push_back(triangleIndex);
        }
    }
}

bool buildStaticCollisionCache(const Model& model, StaticCollisionCache& cache)
{
    constexpr float kGroundNormalMinY = 0.25f;
    constexpr float kWallSlopeMaxAbsY = 0.55f;
    constexpr float kWallGridExpansion = 0.4f;

    cache = StaticCollisionCache{};
    cache.worldTransform = model.worldTransform;

    for (const Mesh& mesh : model.meshes)
    {
        for (const auto& primitive : mesh.primitives)
        {
            if (!primitive || primitive->cpuVertices.empty() || primitive->cpuIndices.size() < 3)
            {
                continue;
            }

            const size_t triangleIndexCount = primitive->cpuIndices.size() - (primitive->cpuIndices.size() % 3);
            for (size_t i = 0; i < triangleIndexCount; i += 3)
            {
                const uint32_t ia = primitive->cpuIndices[i + 0];
                const uint32_t ib = primitive->cpuIndices[i + 1];
                const uint32_t ic = primitive->cpuIndices[i + 2];
                if (ia >= primitive->cpuVertices.size() ||
                    ib >= primitive->cpuVertices.size() ||
                    ic >= primitive->cpuVertices.size())
                {
                    continue;
                }

                CollisionTriangle triangle;
                triangle.a = glm::vec3(model.worldTransform * glm::vec4(primitive->cpuVertices[ia].pos, 1.0f));
                triangle.b = glm::vec3(model.worldTransform * glm::vec4(primitive->cpuVertices[ib].pos, 1.0f));
                triangle.c = glm::vec3(model.worldTransform * glm::vec4(primitive->cpuVertices[ic].pos, 1.0f));

                const glm::vec3 normal = glm::cross(triangle.b - triangle.a, triangle.c - triangle.a);
                const float normalLength = glm::length(normal);
                if (normalLength < 1e-6f)
                {
                    continue;
                }

                triangle.normal = normal / normalLength;
                triangle.minXZ = glm::min(glm::min(glm::vec2(triangle.a.x, triangle.a.z),
                                                   glm::vec2(triangle.b.x, triangle.b.z)),
                                          glm::vec2(triangle.c.x, triangle.c.z));
                triangle.maxXZ = glm::max(glm::max(glm::vec2(triangle.a.x, triangle.a.z),
                                                   glm::vec2(triangle.b.x, triangle.b.z)),
                                          glm::vec2(triangle.c.x, triangle.c.z));
                triangle.minY = std::min({triangle.a.y, triangle.b.y, triangle.c.y});
                triangle.maxY = std::max({triangle.a.y, triangle.b.y, triangle.c.y});

                if (std::abs(triangle.normal.y) >= kGroundNormalMinY)
                {
                    const size_t index = cache.groundTriangles.size();
                    cache.groundTriangles.push_back(triangle);
                    insertTriangleIntoGrid(cache.groundGrid, triangle, index);
                }
                if (std::abs(triangle.normal.y) <= kWallSlopeMaxAbsY)
                {
                    const size_t index = cache.wallTriangles.size();
                    cache.wallTriangles.push_back(triangle);
                    insertTriangleIntoGrid(cache.wallGrid, triangle, index, kWallGridExpansion);
                }
            }
        }
    }

    cache.valid = !cache.groundTriangles.empty() || !cache.wallTriangles.empty();
    return cache.valid;
}

const StaticCollisionCache* staticCollisionCacheForModel(const Model& model)
{
    StaticCollisionCache& cache = g_staticCollisionCaches[&model];
    if (!cache.valid || !matricesApproximatelyEqual(cache.worldTransform, model.worldTransform))
    {
        if (!buildStaticCollisionCache(model, cache))
        {
            return nullptr;
        }
        qDebug() << "[ViewportHost] Static collision cache built for"
                 << QString::fromStdString(model.name)
                 << "groundTris=" << static_cast<qint64>(cache.groundTriangles.size())
                 << "wallTris=" << static_cast<qint64>(cache.wallTriangles.size());
    }
    return &cache;
}

void appendGridCandidates(const std::unordered_map<std::int64_t, std::vector<size_t>>& grid,
                          const glm::vec2& xz,
                          float radius,
                          std::vector<size_t>& outCandidates)
{
    const int minX = collisionCellFor(xz.x - radius);
    const int maxX = collisionCellFor(xz.x + radius);
    const int minZ = collisionCellFor(xz.y - radius);
    const int maxZ = collisionCellFor(xz.y + radius);
    for (int x = minX; x <= maxX; ++x)
    {
        for (int z = minZ; z <= maxZ; ++z)
        {
            const auto found = grid.find(collisionCellKey(x, z));
            if (found == grid.end())
            {
                continue;
            }
            outCandidates.insert(outCandidates.end(), found->second.begin(), found->second.end());
        }
    }
}

bool sampleModelSurfaceYAtXZ(const Model& surfaceModel,
                             const glm::vec2& xz,
                             float maxSurfaceY,
                             float& outSurfaceY)
{
    if (xz.x < surfaceModel.boundsMinWorld.x || xz.x > surfaceModel.boundsMaxWorld.x ||
        xz.y < surfaceModel.boundsMinWorld.z || xz.y > surfaceModel.boundsMaxWorld.z)
    {
        return false;
    }

    const StaticCollisionCache* cache = staticCollisionCacheForModel(surfaceModel);
    if (!cache)
    {
        return false;
    }

    std::vector<size_t> candidates;
    appendGridCandidates(cache->groundGrid, xz, 0.0f, candidates);
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());

    bool found = false;
    float bestY = -std::numeric_limits<float>::infinity();
    for (size_t candidate : candidates)
    {
        if (candidate >= cache->groundTriangles.size())
        {
            continue;
        }

        const CollisionTriangle& triangle = cache->groundTriangles[candidate];
        float u = 0.0f;
        float v = 0.0f;
        float w = 0.0f;
        if (!barycentricInTriangleXZ(triangle.a, triangle.b, triangle.c, xz, u, v, w))
        {
            continue;
        }

        const float y = u * triangle.a.y + v * triangle.b.y + w * triangle.c.y;
        if (y <= maxSurfaceY && y > bestY)
        {
            bestY = y;
            found = true;
        }
    }

    if (found)
    {
        outSurfaceY = bestY;
    }
    return found;
}

glm::vec2 closestPointOnSegmentXZ(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b)
{
    const glm::vec2 ab = b - a;
    const float denom = glm::dot(ab, ab);
    if (denom <= 1e-8f)
    {
        return a;
    }
    const float t = std::clamp(glm::dot(p - a, ab) / denom, 0.0f, 1.0f);
    return a + ab * t;
}

bool closestPointOnTriangleEdgesXZ(const glm::vec2& p,
                                   const glm::vec2& a,
                                   const glm::vec2& b,
                                   const glm::vec2& c,
                                   glm::vec2& outClosest)
{
    const glm::vec2 candidates[3] = {
        closestPointOnSegmentXZ(p, a, b),
        closestPointOnSegmentXZ(p, b, c),
        closestPointOnSegmentXZ(p, c, a)
    };

    float bestDist2 = std::numeric_limits<float>::max();
    bool found = false;
    for (const glm::vec2& candidate : candidates)
    {
        const float dist2 = glm::dot(p - candidate, p - candidate);
        if (dist2 < bestDist2)
        {
            bestDist2 = dist2;
            outClosest = candidate;
            found = true;
        }
    }
    return found;
}

bool resolveCharacterWallsFromScene(Model& character, const Model& sceneModel)
{
    constexpr float kCharacterRadius = 0.32f;
    constexpr float kCollisionSkin = 0.025f;
    constexpr int kIterations = 3;

    const StaticCollisionCache* cache = staticCollisionCacheForModel(sceneModel);
    if (!cache)
    {
        return false;
    }

    glm::vec3 origin = glm::vec3(character.worldTransform[3]);
    const float feetY = character.boundsMinWorld.y - 0.05f;
    const float headY = character.boundsMaxWorld.y + 0.05f;
    bool resolved = false;

    for (int iteration = 0; iteration < kIterations; ++iteration)
    {
        glm::vec2 totalCorrection(0.0f);
        const glm::vec2 posXZ(origin.x, origin.z);
        std::vector<size_t> candidates;
        appendGridCandidates(cache->wallGrid, posXZ, kCharacterRadius + kCollisionSkin, candidates);
        std::sort(candidates.begin(), candidates.end());
        candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());

        for (size_t candidate : candidates)
        {
            if (candidate >= cache->wallTriangles.size())
            {
                continue;
            }

            const CollisionTriangle& triangle = cache->wallTriangles[candidate];
            if (triangle.maxY < feetY || triangle.minY > headY)
            {
                continue;
            }

            glm::vec2 closest(0.0f);
            if (!closestPointOnTriangleEdgesXZ(posXZ,
                                               glm::vec2(triangle.a.x, triangle.a.z),
                                               glm::vec2(triangle.b.x, triangle.b.z),
                                               glm::vec2(triangle.c.x, triangle.c.z),
                                               closest))
            {
                continue;
            }

            glm::vec2 away = posXZ - closest;
            const float dist = glm::length(away);
            const float minDistance = kCharacterRadius + kCollisionSkin;
            if (dist >= minDistance)
            {
                continue;
            }

            if (dist <= 1e-5f)
            {
                away = glm::normalize(glm::vec2(triangle.normal.x, triangle.normal.z));
                if (glm::length(away) <= 1e-5f)
                {
                    continue;
                }
            }
            else
            {
                away /= dist;
            }

            totalCorrection += away * (minDistance - std::max(dist, 0.0f));
        }

        if (glm::length(totalCorrection) <= 1e-5f)
        {
            break;
        }

        origin.x += totalCorrection.x;
        origin.z += totalCorrection.y;
        resolved = true;
    }

    if (!resolved)
    {
        return false;
    }

    const glm::vec3 previousPos = glm::vec3(character.worldTransform[3]);
    glm::mat4 resolvedTransform = character.worldTransform;
    resolvedTransform[3] = glm::vec4(origin, 1.0f);

    glm::vec2 correction(origin.x - previousPos.x, origin.z - previousPos.z);
    if (glm::length(correction) > 1e-5f)
    {
        const glm::vec2 normal = glm::normalize(correction);
        const glm::vec2 planarVelocity(character.character.velocity.x, character.character.velocity.z);
        const float inwardSpeed = glm::dot(planarVelocity, -normal);
        if (inwardSpeed > 0.0f)
        {
            const glm::vec2 adjustedVelocity = planarVelocity + normal * inwardSpeed;
            character.character.velocity.x = adjustedVelocity.x;
            character.character.velocity.z = adjustedVelocity.y;
        }
    }

    character.setWorldTransform(resolvedTransform);
    return true;
}

void updateCharacterGroundFromSceneSurface(Model& character, const Model& surfaceModel)
{
    constexpr float kMaxStepUp = 0.45f;
    constexpr float kPhysicsFloorY = 0.0f;
    const glm::vec3 origin = glm::vec3(character.worldTransform[3]);
    const float footOffset = std::max(0.0f, origin.y - character.boundsMinWorld.y);
    character.character.groundHeight = kPhysicsFloorY + footOffset;

    const float maxSurfaceY = character.boundsMinWorld.y + kMaxStepUp;
    float surfaceY = 0.0f;
    if (sampleModelSurfaceYAtXZ(surfaceModel, glm::vec2(origin.x, origin.z), maxSurfaceY, surfaceY))
    {
        character.character.groundHeight = std::max(kPhysicsFloorY, surfaceY) + footOffset;
    }
}

int findFollowCameraIndexForScene(Display* display, int sceneIndex)
{
    if (!display || sceneIndex < 0)
    {
        return -1;
    }

    const auto& cameras = display->cameras;
    for (size_t i = 0; i < cameras.size(); ++i)
    {
        Camera* camera = cameras[i];
        if (!camera)
        {
            continue;
        }
        if (camera->isFollowModeEnabled() && camera->getFollowSceneIndex() == sceneIndex)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

QVector3D vector3FromJson(const QJsonValue& value)
{
    if (!value.isArray())
    {
        return QVector3D(0.0f, 0.0f, 0.0f);
    }
    const QJsonArray array = value.toArray();
    if (array.size() < 3)
    {
        return QVector3D(0.0f, 0.0f, 0.0f);
    }
    return QVector3D(static_cast<float>(array.at(0).toDouble()),
                     static_cast<float>(array.at(1).toDouble()),
                     static_cast<float>(array.at(2).toDouble()));
}

void appendWarningUnique(QJsonArray& warnings, const QString& warning)
{
    if (warning.isEmpty())
    {
        return;
    }
    for (const QJsonValue& existing : warnings)
    {
        if (existing.toString() == warning)
        {
            return;
        }
    }
    warnings.append(warning);
}

inline std::uint8_t channelFromColor(std::uint32_t color, int shift)
{
    return static_cast<std::uint8_t>((color >> shift) & 0xFFu);
}

void blendPixel(std::vector<std::uint8_t>& pixels,
                int width,
                int height,
                int x,
                int y,
                std::uint32_t color)
{
    if (x < 0 || y < 0 || x >= width || y >= height)
    {
        return;
    }

    const std::uint8_t srcB = channelFromColor(color, 0);
    const std::uint8_t srcG = channelFromColor(color, 8);
    const std::uint8_t srcR = channelFromColor(color, 16);
    const std::uint8_t srcA = channelFromColor(color, 24);
    const size_t idx = static_cast<size_t>(y * width + x) * 4u;
    const std::uint8_t dstA = pixels[idx + 3];
    const std::uint8_t outA = static_cast<std::uint8_t>(
        std::min(255, static_cast<int>(srcA) + static_cast<int>(dstA) * (255 - srcA) / 255));
    if (outA == 0)
    {
        return;
    }

    pixels[idx + 0] = static_cast<std::uint8_t>(
        (srcB * srcA + pixels[idx + 0] * (255 - srcA) / 255) * 255 / outA);
    pixels[idx + 1] = static_cast<std::uint8_t>(
        (srcG * srcA + pixels[idx + 1] * (255 - srcA) / 255) * 255 / outA);
    pixels[idx + 2] = static_cast<std::uint8_t>(
        (srcR * srcA + pixels[idx + 2] * (255 - srcA) / 255) * 255 / outA);
    pixels[idx + 3] = outA;
}

void drawLine(std::vector<std::uint8_t>& pixels,
              int width,
              int height,
              int x0,
              int y0,
              int x1,
              int y1,
              std::uint32_t color)
{
    int dx = std::abs(x1 - x0);
    int sx = (x0 < x1) ? 1 : -1;
    int dy = -std::abs(y1 - y0);
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;

    while (true)
    {
        blendPixel(pixels, width, height, x0, y0, color);
        if (x0 == x1 && y0 == y1)
        {
            break;
        }
        const int e2 = 2 * err;
        if (e2 >= dy)
        {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx)
        {
            err += dx;
            y0 += sy;
        }
    }
}

void drawCircle(std::vector<std::uint8_t>& pixels,
                int width,
                int height,
                int cx,
                int cy,
                int radius,
                std::uint32_t color)
{
    if (radius <= 0)
    {
        blendPixel(pixels, width, height, cx, cy, color);
        return;
    }
    for (int y = -radius; y <= radius; ++y)
    {
        for (int x = -radius; x <= radius; ++x)
        {
            if (x * x + y * y <= radius * radius)
            {
                blendPixel(pixels, width, height, cx + x, cy + y, color);
            }
        }
    }
}

bool worldToScreen(const glm::mat4& view,
                   const glm::mat4& proj,
                   int width,
                   int height,
                   const QVector3D& world,
                   int& outX,
                   int& outY)
{
    const glm::vec4 clip = proj * view * glm::vec4(world.x(), world.y(), world.z(), 1.0f);
    if (std::fabs(clip.w) <= 1e-6f || clip.w <= 0.0f)
    {
        return false;
    }
    const float ndcX = clip.x / clip.w;
    const float ndcY = clip.y / clip.w;
    const float ndcZ = clip.z / clip.w;
    if (ndcX < -1.2f || ndcX > 1.2f || ndcY < -1.2f || ndcY > 1.2f || ndcZ < -0.2f || ndcZ > 1.2f)
    {
        return false;
    }

    const float sx = (ndcX * 0.5f + 0.5f) * static_cast<float>(width);
    const float sy = (1.0f - (ndcY * 0.5f + 0.5f)) * static_cast<float>(height);
    outX = static_cast<int>(std::round(sx));
    outY = static_cast<int>(std::round(sy));
    return true;
}

std::array<bool, 4> decodeMovePattern(QString move)
{
    move = move.toUpper();
    return std::array<bool, 4>{
        move.contains(QStringLiteral("W")),
        move.contains(QStringLiteral("A")),
        move.contains(QStringLiteral("S")),
        move.contains(QStringLiteral("D"))
    };
}

void reconfigurePhysicsBodyForMode(Model& model, motive::IPhysicsWorld& physicsWorld)
{
    if (!couplingRequiresPhysics(model))
    {
        if (model.getPhysicsBody() && !model.character.isControllable)
        {
            model.disablePhysics(physicsWorld);
        }
        return;
    }

    motive::PhysicsBodyConfig config;
    config.shapeType = model.character.isControllable
        ? motive::CollisionShapeType::Capsule
        : motive::CollisionShapeType::Box;
    config.mass = model.character.isControllable ? 70.0f : 1.0f;
    config.friction = model.character.isControllable ? 0.3f : 0.5f;
    config.restitution = 0.0f;
    config.useModelBounds = true;
    config.isCharacter = model.character.isControllable;
    config.isKinematic = couplingUsesKinematicBody(model);
    config.useGravity = model.getUseGravity();
    config.customGravity = model.getCustomGravity();

    model.enablePhysics(physicsWorld, config);
}

QString makeCameraId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

float sanitizedFollowDistance(float distance)
{
    return std::max(followcam::kMinDistance, distance);
}

FollowSettings makeFollowSettings(float yawRadians,
                                  float pitchRadians,
                                  float distance,
                                  float smoothSpeed,
                                  const glm::vec3& targetOffset = glm::vec3(0.0f),
                                  bool enabled = true)
{
    FollowSettings settings;
    settings.relativeYaw = yawRadians;
    settings.relativePitch = pitchRadians;
    settings.distance = distance;
    settings.smoothSpeed = smoothSpeed;
    settings.targetOffset = targetOffset;
    settings.enabled = enabled;
    return followcam::sanitizeSettings(settings);
}

ViewportHostWidget::ViewportLayout normalizedViewportLayout(const ViewportHostWidget::ViewportLayout& layout)
{
    ViewportHostWidget::ViewportLayout normalized;
    normalized.count = std::clamp(layout.count, 1, 4);
    normalized.cameraIds = layout.cameraIds;
    while (normalized.cameraIds.size() < normalized.count)
    {
        normalized.cameraIds.append(QString());
    }
    while (normalized.cameraIds.size() > normalized.count)
    {
        normalized.cameraIds.removeLast();
    }
    return normalized;
}

bool hasFreeCameraConfig(const QList<ViewportHostWidget::CameraConfig>& configs)
{
    for (const auto& config : configs)
    {
        if (config.type == ViewportHostWidget::CameraConfig::Type::Free || !config.isFollowCamera())
        {
            return true;
        }
    }
    return false;
}

QString cameraModeToString(CameraMode mode)
{
    switch (mode)
    {
    case CameraMode::FreeFly:
        return QStringLiteral("FreeFly");
    case CameraMode::CharacterFollow:
        return QStringLiteral("CharacterFollow");
    case CameraMode::OrbitFollow:
        return QStringLiteral("OrbitFollow");
    case CameraMode::Fixed:
        return QStringLiteral("Fixed");
    }
    return QStringLiteral("FreeFly");
}

CameraMode cameraModeFromString(const QString& mode, CameraMode fallback = CameraMode::FreeFly)
{
    if (mode == QStringLiteral("FreeFly")) return CameraMode::FreeFly;
    if (mode == QStringLiteral("CharacterFollow")) return CameraMode::CharacterFollow;
    if (mode == QStringLiteral("OrbitFollow")) return CameraMode::OrbitFollow;
    if (mode == QStringLiteral("Fixed")) return CameraMode::Fixed;
    return fallback;
}

bool isFollowMode(CameraMode mode)
{
    return mode == CameraMode::CharacterFollow || mode == CameraMode::OrbitFollow;
}

QString compassFromForward(const glm::vec3& forward)
{
    const float horizontalLength = std::sqrt(forward.x * forward.x + forward.z * forward.z);
    if (horizontalLength <= 1e-5f)
    {
        return QStringLiteral("no horizontal bearing");
    }

    const float angle = std::atan2(forward.x, -forward.z);
    int sector = static_cast<int>(std::floor((glm::degrees(angle) + 360.0f + 22.5f) / 45.0f)) % 8;
    static const std::array<const char*, 8> kSectors{
        "-Z / North",
        "+X -Z / Northeast",
        "+X / East",
        "+X +Z / Southeast",
        "+Z / South",
        "-X +Z / Southwest",
        "-X / West",
        "-X -Z / Northwest",
    };
    return QString::fromLatin1(kSectors[static_cast<size_t>(sector)]);
}

QString verticalFacingLabel(const glm::vec3& forward)
{
    if (forward.y <= -0.85f)
    {
        return QStringLiteral("LOOKING DOWN");
    }
    if (forward.y >= 0.85f)
    {
        return QStringLiteral("LOOKING UP");
    }
    if (forward.y <= -0.30f)
    {
        return QStringLiteral("LOOKING DOWNWARD");
    }
    if (forward.y >= 0.30f)
    {
        return QStringLiteral("LOOKING UPWARD");
    }
    return QStringLiteral("LOOKING LEVEL");
}

QJsonObject cameraDirectionObject(const Camera& camera)
{
    const glm::vec2 rotation = camera.getEulerRotation();
    const glm::vec3 forward = camera.getForwardVector();
    const float yawDegrees = glm::degrees(rotation.x);
    const float pitchDegrees = glm::degrees(rotation.y);
    const QString vertical = verticalFacingLabel(forward);
    const QString compass = compassFromForward(forward);
    return QJsonObject{
        {QStringLiteral("visible"), true},
        {QStringLiteral("cameraId"), QString::fromStdString(camera.getCameraId())},
        {QStringLiteral("cameraName"), QString::fromStdString(camera.getCameraName())},
        {QStringLiteral("cameraMode"), cameraModeToString(camera.getMode())},
        {QStringLiteral("summary"), QStringLiteral("%1 toward %2").arg(vertical, compass)},
        {QStringLiteral("vertical"), vertical},
        {QStringLiteral("compass"), compass},
        {QStringLiteral("yawDegrees"), yawDegrees},
        {QStringLiteral("pitchDegrees"), pitchDegrees},
        {QStringLiteral("forward"), QJsonArray{forward.x, forward.y, forward.z}},
        {QStringLiteral("lookingDown"), forward.y <= -0.85f},
        {QStringLiteral("lookingUp"), forward.y >= 0.85f}
    };
}

}

ViewportHostWidget::ViewportHostWidget(QWidget* parent)
    : QWidget(parent)
    , m_runtime(std::make_unique<ViewportRuntime>())
    , m_sceneController(std::make_unique<ViewportSceneController>(*m_runtime))
    , m_cameraController(std::make_unique<ViewportCameraController>(*m_runtime, *m_sceneController))
    , m_hierarchyBuilder(std::make_unique<ViewportHierarchyBuilder>(*m_runtime, *m_sceneController, m_sceneLight))
{
    setFocusPolicy(Qt::StrongFocus);
    setAcceptDrops(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_viewportSelectorPanel = new QWidget(this);
    m_viewportSelectorPanel->setStyleSheet(QStringLiteral("background: rgba(16,22,29,220);"));
    m_viewportSelectorGrid = new QGridLayout(m_viewportSelectorPanel);
    m_viewportSelectorGrid->setContentsMargins(8, 8, 8, 8);
    m_viewportSelectorGrid->setHorizontalSpacing(8);
    m_viewportSelectorGrid->setVerticalSpacing(8);
    layout->addWidget(m_viewportSelectorPanel, 0);

    m_renderSurface = new QWidget(this);
    m_renderSurface->setAttribute(Qt::WA_NativeWindow, true);
    m_renderSurface->setFocusPolicy(Qt::StrongFocus);
    m_renderSurface->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* renderLayout = new QVBoxLayout(m_renderSurface);
    renderLayout->setContentsMargins(0, 0, 0, 0);
    renderLayout->setSpacing(0);
    m_statusLabel = new QLabel(QStringLiteral("Initializing viewport..."), m_renderSurface);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    renderLayout->addWidget(m_statusLabel);
    m_cameraDirectionLabel = new QLabel(m_renderSurface);
    m_cameraDirectionLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_cameraDirectionLabel->setTextFormat(Qt::RichText);
    m_cameraDirectionLabel->setStyleSheet(QStringLiteral(
        "QLabel {"
        " background: rgba(3, 8, 13, 220);"
        " color: #f8fafc;"
        " border: 2px solid #fbbf24;"
        " border-radius: 10px;"
        " padding: 8px 10px;"
        " font-family: 'DejaVu Sans Mono';"
        " font-size: 13px;"
        "}"));
    m_cameraDirectionLabel->setMinimumWidth(260);
    m_cameraDirectionLabel->hide();
    layout->addWidget(m_renderSurface, 1);

    m_renderTimer.setInterval(16);
    connect(&m_renderTimer, &QTimer::timeout, this, [this]() { renderFrame(); });

    m_viewportLayout = normalizedViewportLayout(m_viewportLayout);
    syncViewportSelectorChoices();
}

ViewportHostWidget::~ViewportHostWidget()
{
    m_renderTimer.stop();
    m_initialized = false;

    for (QComboBox* combo : m_viewportCameraSelectors)
    {
        if (combo)
        {
            combo->hide();
            combo->deleteLater();
        }
    }
    for (QFrame* frame : m_viewportBorders)
    {
        if (frame)
        {
            frame->hide();
            frame->deleteLater();
        }
    }

    if (m_runtime)
    {
        if (Display* display = m_runtime->display())
        {
            display->setMouseButtonEventCallback(nullptr);
        }
        m_runtime->shutdown();
    }
}

void ViewportHostWidget::loadAssetFromPath(const QString& path)
{
    qDebug() << "[ViewportHost] loadAssetFromPath" << path;
    m_tpsBootstrapPending = true;
    m_tpsBootstrapApplied = false;
    m_sceneController->loadAssetFromPath(path);
}

void ViewportHostWidget::loadSceneFromItems(const QList<SceneItem>& items)
{
    qDebug() << "[ViewportHost] loadSceneFromItems count=" << items.size();
    m_tpsBootstrapPending = true;
    m_tpsBootstrapApplied = false;
    m_sceneController->loadSceneFromItems(items);
    bootstrapThirdPersonShooter(false);
    notifySceneChanged();
}

void ViewportHostWidget::refresh()
{
    qDebug() << "[ViewportHost] Refreshing viewport";
    notifySceneChanged();
}

void ViewportHostWidget::setCustomOverlayBitmap(const glyph::OverlayBitmap& bitmap)
{
    if (!m_runtime || !m_runtime->display())
    {
        return;
    }
    m_runtime->display()->setCustomOverlayBitmap(bitmap, true);
}

void ViewportHostWidget::clearCustomOverlayBitmap()
{
    if (!m_runtime || !m_runtime->display())
    {
        return;
    }
    m_runtime->display()->clearCustomOverlayBitmap();
}

QString ViewportHostWidget::currentAssetPath() const
{
    return m_sceneController->currentAssetPath();
}

QList<ViewportHostWidget::SceneItem> ViewportHostWidget::sceneItems() const
{
    return m_sceneController->sceneItems();
}

QList<ViewportHostWidget::HierarchyNode> ViewportHostWidget::hierarchyItems() const
{
    return m_hierarchyBuilder->hierarchyItems();
}

QJsonArray ViewportHostWidget::hierarchyJson() const
{
    return m_hierarchyBuilder->hierarchyJson();
}

QJsonArray ViewportHostWidget::sceneProfileJson() const
{
    return m_hierarchyBuilder->sceneProfileJson();
}

QJsonObject ViewportHostWidget::cameraTrackingDebugJson() const
{
    QJsonObject debug;

    Display* display = m_runtime ? m_runtime->display() : nullptr;
    Engine* engine = m_runtime ? m_runtime->engine() : nullptr;
    Camera* camera = display ? display->getActiveCamera() : nullptr;
    if (!camera)
    {
        debug.insert(QStringLiteral("ok"), false);
        debug.insert(QStringLiteral("error"), QStringLiteral("no active camera"));
        return debug;
    }

    const glm::vec2 rot = camera->getEulerRotation();
    const glm::vec3 front = camera->getForwardVector();

    debug.insert(QStringLiteral("ok"), true);
    debug.insert(QStringLiteral("cameraId"), QString::fromStdString(camera->getCameraId()));
    debug.insert(QStringLiteral("cameraName"), QString::fromStdString(camera->getCameraName()));
    debug.insert(QStringLiteral("cameraMode"), cameraModeToString(camera->getMode()));
    debug.insert(QStringLiteral("freeFly"), camera->isFreeFlyCamera());
    debug.insert(QStringLiteral("followMode"), camera->isFollowModeEnabled());
    debug.insert(QStringLiteral("followSceneIndex"), camera->getFollowSceneIndex());
    debug.insert(QStringLiteral("cameraPos"), QJsonArray{camera->cameraPos.x, camera->cameraPos.y, camera->cameraPos.z});
    debug.insert(QStringLiteral("cameraRot"), QJsonArray{rot.x, rot.y});
    debug.insert(QStringLiteral("cameraFront"), QJsonArray{front.x, front.y, front.z});
    debug.insert(QStringLiteral("directionIndicator"), cameraDirectionObject(*camera));
    debug.insert(QStringLiteral("rightMouseDown"), camera->rightMouseDown);
    debug.insert(QStringLiteral("temporaryOrbitDrag"), camera->temporaryOrbitDrag);
    debug.insert(QStringLiteral("focusedViewportIndex"), focusedViewportIndex());
    debug.insert(QStringLiteral("focusedViewportCameraId"), focusedViewportCameraId());

    QJsonArray warnings;

    Model* target = nullptr;
    int targetSceneIndex = -1;
    if (engine)
    {
        const auto& models = engine->models;
        
        // First check if camera has a follow target
        if (camera->isFollowModeEnabled())
        {
            const int followSceneIndex = camera->getFollowSceneIndex();
            if (followSceneIndex >= 0 && followSceneIndex < static_cast<int>(models.size()))
            {
                target = models[static_cast<size_t>(followSceneIndex)].get();
                targetSceneIndex = followSceneIndex;
            }
        }
        
        // If no follow target, check if camera is in CharacterFollow mode
        if (!target && camera->getMode() == CameraMode::CharacterFollow)
        {
            // In CharacterFollow mode, the camera should be following a character
            // Use getFollowTarget to get the current follow target
            target = camera->getFollowTarget(models);
            if (target)
            {
                // Find the scene index
                for (int i = 0; i < static_cast<int>(models.size()); ++i)
                {
                    if (models[static_cast<size_t>(i)].get() == target)
                    {
                        targetSceneIndex = i;
                        break;
                    }
                }
            }
        }
    }

    if (!target)
    {
        debug.insert(QStringLiteral("hasTarget"), false);
        warnings.append(QStringLiteral("NO_TARGET_BOUND_TO_CAMERA"));
        debug.insert(QStringLiteral("warnings"), warnings);
        return debug;
    }

    const glm::vec3 modelTargetPos = target->getFollowAnchorPosition();
    const bool usingTrackedTarget = camera->hasTrackedFollowTarget();
    const glm::vec3 targetPos = usingTrackedTarget ? camera->getTrackedFollowTarget() : modelTargetPos;
    const glm::vec3 targetPosRaw = usingTrackedTarget ? camera->getTrackedFollowTargetRaw() : modelTargetPos;
    const glm::vec3 targetPosMotion = usingTrackedTarget ? camera->getTrackedFollowTargetMotion() : targetPos;
    const glm::vec3 toTarget = targetPos - camera->cameraPos;
    const float distance = glm::length(toTarget);
    glm::vec3 toTargetDir(0.0f, 0.0f, -1.0f);
    if (distance > 1e-6f)
    {
        toTargetDir = toTarget / distance;
    }
    const float frontDotToTarget = glm::dot(front, toTargetDir);

    const glm::vec4 clip = camera->getProjectionMatrix() * camera->getViewMatrix() * glm::vec4(targetPos, 1.0f);
    const glm::vec4 clipRaw = camera->getProjectionMatrix() * camera->getViewMatrix() * glm::vec4(targetPosRaw, 1.0f);
    const glm::vec4 clipModel = camera->getProjectionMatrix() * camera->getViewMatrix() * glm::vec4(modelTargetPos, 1.0f);
    float ndcX = 0.0f;
    float ndcY = 0.0f;
    float ndcZ = 0.0f;
    float ndcRawX = 0.0f;
    float ndcRawY = 0.0f;
    float ndcRawZ = 0.0f;
    float ndcModelX = 0.0f;
    float ndcModelY = 0.0f;
    float ndcModelZ = 0.0f;
    bool clipValid = std::fabs(clip.w) > 1e-6f;
    const bool clipRawValid = std::fabs(clipRaw.w) > 1e-6f;
    const bool clipModelValid = std::fabs(clipModel.w) > 1e-6f;
    if (clipValid)
    {
        ndcX = clip.x / clip.w;
        ndcY = clip.y / clip.w;
        ndcZ = clip.z / clip.w;
    }
    if (clipRawValid)
    {
        ndcRawX = clipRaw.x / clipRaw.w;
        ndcRawY = clipRaw.y / clipRaw.w;
        ndcRawZ = clipRaw.z / clipRaw.w;
    }
    if (clipModelValid)
    {
        ndcModelX = clipModel.x / clipModel.w;
        ndcModelY = clipModel.y / clipModel.w;
        ndcModelZ = clipModel.z / clipModel.w;
    }

    const bool behind = frontDotToTarget < 0.0f;
    const bool offscreen = !clipValid ||
                           std::fabs(ndcX) > 1.0f ||
                           std::fabs(ndcY) > 1.0f ||
                           ndcZ < 0.0f ||
                           ndcZ > 1.0f;

    if (behind)
    {
        warnings.append(QStringLiteral("TARGET_BEHIND_CAMERA"));
    }
    if (offscreen)
    {
        warnings.append(QStringLiteral("TARGET_OFFSCREEN_OR_CLIPPED"));
    }
    if (distance < 0.5f)
    {
        warnings.append(QStringLiteral("TARGET_TOO_CLOSE_TO_CAMERA"));
    }

    debug.insert(QStringLiteral("hasTarget"), true);
    debug.insert(QStringLiteral("targetSceneIndex"), targetSceneIndex);
    debug.insert(QStringLiteral("targetPos"), QJsonArray{targetPos.x, targetPos.y, targetPos.z});
    debug.insert(QStringLiteral("targetPosRaw"), QJsonArray{targetPosRaw.x, targetPosRaw.y, targetPosRaw.z});
    debug.insert(QStringLiteral("targetPosMotion"), QJsonArray{targetPosMotion.x, targetPosMotion.y, targetPosMotion.z});
    debug.insert(QStringLiteral("targetPosModelAnchor"), QJsonArray{modelTargetPos.x, modelTargetPos.y, modelTargetPos.z});
    debug.insert(QStringLiteral("targetTrackingSource"), usingTrackedTarget ? QStringLiteral("cameraTracker") : QStringLiteral("modelAnchor"));
    QString targetAnchorMode = QStringLiteral("worldTransform");
    bool targetAnchorReferencesPreprocessedFrames = false;
    if (target->character.isControllable && target->followAnchorLocalCenterInitialized)
    {
        targetAnchorMode = QStringLiteral("stableLocalAnchor");
    }
    else if (target->boundsRadius > 0.0f)
    {
        targetAnchorMode = QStringLiteral("preprocessedAnimatedBounds");
        targetAnchorReferencesPreprocessedFrames = true;
    }
    debug.insert(QStringLiteral("targetAnchorMode"), targetAnchorMode);
    debug.insert(QStringLiteral("targetAnchorReferencesPreprocessedFrames"), targetAnchorReferencesPreprocessedFrames);
    debug.insert(QStringLiteral("targetAnimationPreprocessedFrameValid"), target->animationPreprocessedFrameValid);
    debug.insert(QStringLiteral("targetAnimationPreprocessedFrameCounter"), static_cast<qint64>(target->animationPreprocessedFrameCounter));
    debug.insert(QStringLiteral("targetVelocity"), QJsonArray{target->character.velocity.x, target->character.velocity.y, target->character.velocity.z});
    debug.insert(QStringLiteral("targetControllable"), target->character.isControllable);
    debug.insert(QStringLiteral("distanceToTarget"), distance);
    debug.insert(QStringLiteral("frontDotToTarget"), frontDotToTarget);
    debug.insert(QStringLiteral("targetNdc"), QJsonArray{ndcX, ndcY, ndcZ});
    debug.insert(QStringLiteral("targetNdcRaw"), QJsonArray{ndcRawX, ndcRawY, ndcRawZ});
    debug.insert(QStringLiteral("targetNdcModelAnchor"), QJsonArray{ndcModelX, ndcModelY, ndcModelZ});
    debug.insert(QStringLiteral("targetClipW"), clip.w);
    debug.insert(QStringLiteral("targetBehindCamera"), behind);
    debug.insert(QStringLiteral("targetOffscreen"), offscreen);
    debug.insert(QStringLiteral("warnings"), warnings);
    if (!warnings.isEmpty())
    {
        static QString s_lastWarningKey;
        static auto s_lastWarningLog = std::chrono::steady_clock::time_point{};

        QStringList warningValues;
        warningValues.reserve(warnings.size());
        for (const auto& warning : warnings)
        {
            warningValues.append(warning.toString());
        }
        const QString warningKey = QStringLiteral("%1|%2|%3")
            .arg(QString::fromStdString(camera->getCameraId()))
            .arg(targetSceneIndex)
            .arg(warningValues.join(QStringLiteral(",")));

        const auto now = std::chrono::steady_clock::now();
        const bool warningChanged = (warningKey != s_lastWarningKey);
        const bool cooldownElapsed = (s_lastWarningLog.time_since_epoch().count() == 0) ||
            (std::chrono::duration_cast<std::chrono::milliseconds>(now - s_lastWarningLog).count() >= 1000);
        if (warningChanged || cooldownElapsed)
        {
            qWarning().nospace()
                << "[CameraTracking] camera=\"" << QString::fromStdString(camera->getCameraName())
                << "\" id=" << QString::fromStdString(camera->getCameraId())
                << " followSceneIndex=" << camera->getFollowSceneIndex()
                << " targetSceneIndex=" << targetSceneIndex
                << " dist=" << distance
                << " frontDot=" << frontDotToTarget
                << " ndc=(" << ndcX << "," << ndcY << "," << ndcZ << ")"
                << " behind=" << (behind ? "true" : "false")
                << " offscreen=" << (offscreen ? "true" : "false")
                << " warnings=" << warnings;
            s_lastWarningKey = warningKey;
            s_lastWarningLog = now;
        }
    }
    return debug;
}

QJsonObject ViewportHostWidget::motionDebugSampleToJson(const MotionDebugSample& sample) const
{
    return QJsonObject{
        {QStringLiteral("frame"), static_cast<qint64>(sample.frame)},
        {QStringLiteral("elapsedSeconds"), sample.elapsedSeconds},
        {QStringLiteral("deltaSeconds"), sample.deltaSeconds},
        {QStringLiteral("cameraId"), sample.cameraId},
        {QStringLiteral("cameraName"), sample.cameraName},
        {QStringLiteral("cameraMode"), sample.cameraMode},
        {QStringLiteral("targetSceneIndex"), sample.targetSceneIndex},
        {QStringLiteral("cameraPos"), QJsonArray{sample.cameraPos.x(), sample.cameraPos.y(), sample.cameraPos.z()}},
        {QStringLiteral("targetPos"), QJsonArray{sample.targetPos.x(), sample.targetPos.y(), sample.targetPos.z()}},
        {QStringLiteral("targetPosRaw"), QJsonArray{sample.targetPosRaw.x(), sample.targetPosRaw.y(), sample.targetPosRaw.z()}},
        {QStringLiteral("targetPosMotion"), QJsonArray{sample.targetPosMotion.x(), sample.targetPosMotion.y(), sample.targetPosMotion.z()}},
        {QStringLiteral("targetVelocity"), QJsonArray{sample.targetVelocity.x(), sample.targetVelocity.y(), sample.targetVelocity.z()}},
        {QStringLiteral("distanceToTarget"), sample.distanceToTarget},
        {QStringLiteral("frontDotToTarget"), sample.frontDotToTarget},
        {QStringLiteral("followDistance"), sample.followDistance},
        {QStringLiteral("followSmoothSpeed"), sample.followSmoothSpeed},
        {QStringLiteral("targetJitterMagnitude"), sample.targetJitterMagnitude},
        {QStringLiteral("cameraStepMagnitude"), sample.cameraStepMagnitude},
        {QStringLiteral("distanceDelta"), sample.distanceDelta},
        {QStringLiteral("distanceDeltaFlipCount"), sample.distanceDeltaFlipCount},
        {QStringLiteral("oscillationSuspected"), sample.oscillationSuspected},
        {QStringLiteral("warnings"), sample.warnings}
    };
}

QJsonObject ViewportHostWidget::motionDebugFrameJson() const
{
    std::lock_guard<std::mutex> lock(m_motionDebugMutex);
    if (m_motionDebugHistory.empty())
    {
        return QJsonObject{
            {QStringLiteral("ok"), false},
            {QStringLiteral("error"), QStringLiteral("no motion samples captured")}
        };
    }

    QJsonObject payload = motionDebugSampleToJson(m_motionDebugHistory.back());
    payload.insert(QStringLiteral("ok"), true);
    payload.insert(QStringLiteral("historySize"), static_cast<int>(m_motionDebugHistory.size()));
    return payload;
}

QJsonArray ViewportHostWidget::motionDebugHistoryJson(int maxFrames, int sceneIndex) const
{
    const int clampedMaxFrames = std::clamp(maxFrames, 1, kMotionDebugHistoryCapacity);
    QJsonArray history;

    std::lock_guard<std::mutex> lock(m_motionDebugMutex);
    int emitted = 0;
    for (auto it = m_motionDebugHistory.rbegin();
         it != m_motionDebugHistory.rend() && emitted < clampedMaxFrames;
         ++it)
    {
        if (sceneIndex >= 0 && it->targetSceneIndex != sceneIndex)
        {
            continue;
        }
        history.prepend(motionDebugSampleToJson(*it));
        ++emitted;
    }
    return history;
}

QJsonObject ViewportHostWidget::motionDebugSummaryJson() const
{
    std::lock_guard<std::mutex> lock(m_motionDebugMutex);
    QJsonObject summary;
    summary.insert(QStringLiteral("ok"), true);
    summary.insert(QStringLiteral("historySize"), static_cast<int>(m_motionDebugHistory.size()));
    if (m_motionDebugHistory.empty())
    {
        summary.insert(QStringLiteral("sampleCount"), 0);
        summary.insert(QStringLiteral("oscillationDetected"), false);
        return summary;
    }

    const int sampleCount = std::min<int>(kMotionDebugSummaryWindow, static_cast<int>(m_motionDebugHistory.size()));
    float maxJitter = 0.0f;
    float maxCameraStep = 0.0f;
    float meanJitter = 0.0f;
    int oscillationFrames = 0;
    int warningsCount = 0;
    for (int i = static_cast<int>(m_motionDebugHistory.size()) - sampleCount;
         i < static_cast<int>(m_motionDebugHistory.size());
         ++i)
    {
        const MotionDebugSample& sample = m_motionDebugHistory[static_cast<size_t>(i)];
        maxJitter = std::max(maxJitter, sample.targetJitterMagnitude);
        maxCameraStep = std::max(maxCameraStep, sample.cameraStepMagnitude);
        meanJitter += sample.targetJitterMagnitude;
        if (sample.oscillationSuspected)
        {
            ++oscillationFrames;
        }
        warningsCount += sample.warnings.size();
    }

    meanJitter /= static_cast<float>(std::max(1, sampleCount));
    summary.insert(QStringLiteral("sampleCount"), sampleCount);
    summary.insert(QStringLiteral("windowFrames"), kMotionDebugSummaryWindow);
    summary.insert(QStringLiteral("maxTargetJitter"), maxJitter);
    summary.insert(QStringLiteral("meanTargetJitter"), meanJitter);
    summary.insert(QStringLiteral("maxCameraStep"), maxCameraStep);
    summary.insert(QStringLiteral("oscillationFrames"), oscillationFrames);
    summary.insert(QStringLiteral("oscillationDetected"), oscillationFrames > 0);
    summary.insert(QStringLiteral("warningsObserved"), warningsCount);
    return summary;
}

void ViewportHostWidget::resetMotionDebug()
{
    std::lock_guard<std::mutex> lock(m_motionDebugMutex);
    m_motionDebugHistory.clear();
    m_motionDebugFrameCounter = 0;
    m_motionDebugElapsedSeconds = 0.0;
    m_motionDebugHasLast = false;
    m_motionDebugLastCameraPos = QVector3D(0.0f, 0.0f, 0.0f);
    m_motionDebugLastDistance = 0.0f;
    m_motionDebugLastDistanceDelta = 0.0f;
    m_motionDebugDistanceFlipCount = 0;
}

void ViewportHostWidget::captureMotionDebugFrame(float dt)
{
    std::lock_guard<std::mutex> lock(m_motionDebugMutex);
    MotionDebugSample sample;
    sample.deltaSeconds = dt;
    sample.elapsedSeconds = m_motionDebugElapsedSeconds;
    sample.frame = ++m_motionDebugFrameCounter;
    m_motionDebugElapsedSeconds += static_cast<double>(std::max(dt, 0.0f));

    const QJsonObject tracking = cameraTrackingDebugJson();
    sample.cameraId = tracking.value(QStringLiteral("cameraId")).toString();
    sample.cameraName = tracking.value(QStringLiteral("cameraName")).toString();
    sample.cameraMode = tracking.value(QStringLiteral("cameraMode")).toString();
    sample.targetSceneIndex = tracking.value(QStringLiteral("targetSceneIndex")).toInt(-1);
    sample.cameraPos = vector3FromJson(tracking.value(QStringLiteral("cameraPos")));
    sample.targetPos = vector3FromJson(tracking.value(QStringLiteral("targetPos")));
    sample.targetPosRaw = vector3FromJson(tracking.value(QStringLiteral("targetPosRaw")));
    sample.targetPosMotion = vector3FromJson(tracking.value(QStringLiteral("targetPosMotion")));
    sample.targetVelocity = vector3FromJson(tracking.value(QStringLiteral("targetVelocity")));
    sample.distanceToTarget = static_cast<float>(tracking.value(QStringLiteral("distanceToTarget")).toDouble(0.0));
    sample.frontDotToTarget = static_cast<float>(tracking.value(QStringLiteral("frontDotToTarget")).toDouble(0.0));
    sample.warnings = tracking.value(QStringLiteral("warnings")).toArray();

    if (m_runtime && m_runtime->display())
    {
        if (Camera* activeCamera = m_runtime->display()->getActiveCamera())
        {
            if (activeCamera->isFollowModeEnabled())
            {
                const FollowSettings& settings = activeCamera->getFollowSettings();
                sample.followDistance = settings.distance;
                sample.followSmoothSpeed = settings.smoothSpeed;
            }
        }
    }

    const QVector3D jitterVec = sample.targetPos - sample.targetPosRaw;
    sample.targetJitterMagnitude = jitterVec.length();
    if (m_motionDebugHasLast)
    {
        sample.cameraStepMagnitude = (sample.cameraPos - m_motionDebugLastCameraPos).length();
        sample.distanceDelta = sample.distanceToTarget - m_motionDebugLastDistance;
        const bool significantCurrent = std::fabs(sample.distanceDelta) > 1e-4f;
        const bool significantLast = std::fabs(m_motionDebugLastDistanceDelta) > 1e-4f;
        const bool signFlip = significantCurrent &&
                              significantLast &&
                              ((sample.distanceDelta > 0.0f && m_motionDebugLastDistanceDelta < 0.0f) ||
                               (sample.distanceDelta < 0.0f && m_motionDebugLastDistanceDelta > 0.0f));
        if (signFlip)
        {
            m_motionDebugDistanceFlipCount = std::min(m_motionDebugDistanceFlipCount + 1, 1000);
        }
        else
        {
            m_motionDebugDistanceFlipCount = std::max(0, m_motionDebugDistanceFlipCount - 1);
        }
    }
    sample.distanceDeltaFlipCount = m_motionDebugDistanceFlipCount;
    sample.oscillationSuspected = sample.distanceDeltaFlipCount >= 4;

    // This metric represents tracker lag (raw vs damped center), not floating-point
    // noise. Use a practical world-space threshold to avoid warning spam.
    const float adaptiveJitterEpsilon = std::max(
        0.30f,
        std::max(0.12f, sample.followDistance * 0.02f));
    if (sample.targetJitterMagnitude > adaptiveJitterEpsilon)
    {
        appendWarningUnique(sample.warnings, QStringLiteral("TARGET_JITTER_ABOVE_EPSILON"));
    }
    if (sample.oscillationSuspected)
    {
        appendWarningUnique(sample.warnings, QStringLiteral("DISTANCE_OSCILLATION_SUSPECTED"));
    }

    m_motionDebugHasLast = true;
    m_motionDebugLastCameraPos = sample.cameraPos;
    m_motionDebugLastDistance = sample.distanceToTarget;
    m_motionDebugLastDistanceDelta = sample.distanceDelta;

    m_motionDebugHistory.push_back(std::move(sample));
    while (static_cast<int>(m_motionDebugHistory.size()) > kMotionDebugHistoryCapacity)
    {
        m_motionDebugHistory.pop_front();
    }
}

void ViewportHostWidget::updateMotionDebugOverlay()
{
    if (!m_runtime || !m_runtime->display())
    {
        return;
    }

    Display* display = m_runtime->display();
    if (!m_motionDebugOverlayOptions.enabled)
    {
        display->clearCustomOverlayBitmap();
        return;
    }

    const int overlayWidth = std::max(1, m_renderSurface ? m_renderSurface->width() : width());
    const int overlayHeight = std::max(1, m_renderSurface ? m_renderSurface->height() : height());

    MotionDebugSample sample;
    std::vector<MotionDebugSample> trailSamples;
    {
        std::lock_guard<std::mutex> lock(m_motionDebugMutex);
        if (m_motionDebugHistory.empty())
        {
            display->clearCustomOverlayBitmap();
            return;
        }
        sample = m_motionDebugHistory.back();
        const int desiredTrailFrames = std::clamp(m_motionDebugOverlayOptions.trailFrames, 2, 240);
        trailSamples.reserve(static_cast<size_t>(desiredTrailFrames));
        for (auto it = m_motionDebugHistory.rbegin();
             it != m_motionDebugHistory.rend() &&
             static_cast<int>(trailSamples.size()) < desiredTrailFrames;
             ++it)
        {
            if (it->targetSceneIndex != sample.targetSceneIndex)
            {
                continue;
            }
            trailSamples.push_back(*it);
        }
        std::reverse(trailSamples.begin(), trailSamples.end());
    }

    Camera* activeCamera = display->getActiveCamera();
    if (!activeCamera)
    {
        display->clearCustomOverlayBitmap();
        return;
    }

    glyph::OverlayBitmap bitmap;
    bitmap.width = static_cast<uint32_t>(overlayWidth);
    bitmap.height = static_cast<uint32_t>(overlayHeight);
    bitmap.offsetX = 0;
    bitmap.offsetY = 0;
    bitmap.pixels.assign(static_cast<size_t>(overlayWidth) * static_cast<size_t>(overlayHeight) * 4u, 0u);

    const glm::mat4 view = activeCamera->getViewMatrix();
    const glm::mat4 proj = activeCamera->getProjectionMatrix();

    int targetX = 0, targetY = 0;
    const bool haveTarget = worldToScreen(view, proj, overlayWidth, overlayHeight, sample.targetPos, targetX, targetY);
    const int screenCenterX = overlayWidth / 2;
    const int screenCenterY = overlayHeight / 2;

    if (m_motionDebugOverlayOptions.showCameraToTargetLine && haveTarget)
    {
        // In camera view-space, the eye is screen center; drawing from projected
        // camera world position is undefined.
        drawLine(bitmap.pixels, overlayWidth, overlayHeight, screenCenterX, screenCenterY, targetX, targetY, 0xAA00CCFFu);
        drawCircle(bitmap.pixels, overlayWidth, overlayHeight, screenCenterX, screenCenterY, 3, 0xCC00CCFFu);
    }

    if (!trailSamples.empty() && (m_motionDebugOverlayOptions.showMotionTrail || m_motionDebugOverlayOptions.showRawTrail))
    {
        auto drawTrail = [&](bool rawSpace, std::uint32_t baseColor)
        {
            int prevX = 0;
            int prevY = 0;
            bool havePrev = false;
            const int count = static_cast<int>(trailSamples.size());
            for (int i = 0; i < count; ++i)
            {
                const MotionDebugSample& frame = trailSamples[static_cast<size_t>(i)];
                const QVector3D worldPos = rawSpace ? frame.targetPosRaw : frame.targetPosMotion;
                int px = 0;
                int py = 0;
                if (!worldToScreen(view, proj, overlayWidth, overlayHeight, worldPos, px, py))
                {
                    havePrev = false;
                    continue;
                }

                const float t = count > 1 ? static_cast<float>(i) / static_cast<float>(count - 1) : 1.0f;
                const std::uint8_t alpha = static_cast<std::uint8_t>(std::clamp(32.0f + t * 223.0f, 32.0f, 255.0f));
                const std::uint32_t color = (baseColor & 0x00FFFFFFu) | (static_cast<std::uint32_t>(alpha) << 24);
                drawCircle(bitmap.pixels, overlayWidth, overlayHeight, px, py, 1, color);
                if (havePrev)
                {
                    drawLine(bitmap.pixels, overlayWidth, overlayHeight, prevX, prevY, px, py, color);
                }
                prevX = px;
                prevY = py;
                havePrev = true;
            }
        };

        if (m_motionDebugOverlayOptions.showMotionTrail)
        {
            drawTrail(false, 0x00A5FFFFu);
        }
        if (m_motionDebugOverlayOptions.showRawTrail)
        {
            drawTrail(true, 0x00FF66FFu);
        }
    }

    if (m_motionDebugOverlayOptions.showTargetMarkers && haveTarget)
    {
        int rawX = 0, rawY = 0, motionX = 0, motionY = 0;
        const bool haveRaw = worldToScreen(view, proj, overlayWidth, overlayHeight, sample.targetPosRaw, rawX, rawY);
        const bool haveMotion = worldToScreen(view, proj, overlayWidth, overlayHeight, sample.targetPosMotion, motionX, motionY);
        drawCircle(bitmap.pixels, overlayWidth, overlayHeight, targetX, targetY, 4, 0xFF20D0FFu);
        if (haveRaw)
        {
            drawCircle(bitmap.pixels, overlayWidth, overlayHeight, rawX, rawY, 3, 0xFF00FF00u);
        }
        if (haveMotion)
        {
            drawCircle(bitmap.pixels, overlayWidth, overlayHeight, motionX, motionY, 3, 0xFF00A5FFu);
        }
    }

    if (m_motionDebugOverlayOptions.showVelocityVector)
    {
        const QVector3D velocityEnd = sample.targetPos +
            (sample.targetVelocity * m_motionDebugOverlayOptions.velocityScale);
        int velEndX = 0, velEndY = 0;
        if (haveTarget &&
            worldToScreen(view, proj, overlayWidth, overlayHeight, velocityEnd, velEndX, velEndY))
        {
            drawLine(bitmap.pixels, overlayWidth, overlayHeight, targetX, targetY, velEndX, velEndY, 0xFF40FFFFu);
            drawCircle(bitmap.pixels, overlayWidth, overlayHeight, velEndX, velEndY, 2, 0xFF40FFFFu);
        }
    }

    if (m_motionDebugOverlayOptions.showScreenCenterCrosshair)
    {
        drawLine(bitmap.pixels, overlayWidth, overlayHeight, screenCenterX - 8, screenCenterY, screenCenterX + 8, screenCenterY, 0xCCFFFFFFu);
        drawLine(bitmap.pixels, overlayWidth, overlayHeight, screenCenterX, screenCenterY - 8, screenCenterX, screenCenterY + 8, 0xCCFFFFFFu);
    }

    display->setCustomOverlayBitmap(bitmap, true);
}

void ViewportHostWidget::updateCameraDirectionIndicator()
{
    if (!m_cameraDirectionLabel)
    {
        return;
    }

    Camera* camera = focusedViewportCamera();
    if (!camera && m_runtime && m_runtime->display())
    {
        camera = m_runtime->display()->getActiveCamera();
    }
    if (!camera)
    {
        m_cameraDirectionLabel->hide();
        if (m_runtime && m_runtime->display())
        {
            m_runtime->display()->clearCustomOverlayBitmap();
        }
        return;
    }

    const QJsonObject direction = cameraDirectionObject(*camera);
    const QJsonArray forward = direction.value(QStringLiteral("forward")).toArray();
    const QString cameraName = direction.value(QStringLiteral("cameraName")).toString(QStringLiteral("Camera"));
    const QString mode = direction.value(QStringLiteral("cameraMode")).toString();
    const QString vertical = direction.value(QStringLiteral("vertical")).toString();
    const QString compass = direction.value(QStringLiteral("compass")).toString();
    const double yaw = direction.value(QStringLiteral("yawDegrees")).toDouble(0.0);
    const double pitch = direction.value(QStringLiteral("pitchDegrees")).toDouble(0.0);
    const double fx = forward.size() == 3 ? forward.at(0).toDouble(0.0) : 0.0;
    const double fy = forward.size() == 3 ? forward.at(1).toDouble(0.0) : 0.0;
    const double fz = forward.size() == 3 ? forward.at(2).toDouble(0.0) : -1.0;

    if (m_runtime && m_runtime->display())
    {
        const uint32_t panelWidth = static_cast<uint32_t>(std::clamp(
            m_renderSurface ? m_renderSurface->width() - 28 : width() - 28,
            320,
            920));
        constexpr uint32_t kPanelHeight = 58;
        motive::text::TextOverlayStyle style;
        style.textColor = 0xFFFFFFFFu;
        style.backgroundColor = 0xE005080Du;
        style.shadowColor = 0xCC000000u;
        style.outlineColor = 0xFF000000u;
        style.drawShadow = true;
        style.drawOutline = true;
        style.drawBackground = true;
        const std::string overlayText = QStringLiteral("%1 | %2 | yaw %3 deg pitch %4 deg | F[%5,%6,%7]")
            .arg(vertical,
                 compass,
                 QString::number(yaw, 'f', 1),
                 QString::number(pitch, 'f', 1),
                 QString::number(fx, 'f', 2),
                 QString::number(fy, 'f', 2),
                 QString::number(fz, 'f', 2))
            .toStdString();
        glyph::OverlayBitmap overlay = motive::text::buildStyledTextOverlay(
            panelWidth,
            kPanelHeight,
            overlayText,
            22,
            motive::text::FontRenderOptions{},
            style);
        overlay.offsetX = 14;
        overlay.offsetY = 14;
        m_runtime->display()->setCustomOverlayBitmap(overlay, true);
    }

    const QString accent = direction.value(QStringLiteral("lookingDown")).toBool(false)
        ? QStringLiteral("#38bdf8")
        : (direction.value(QStringLiteral("lookingUp")).toBool(false) ? QStringLiteral("#f97316")
                                                                       : QStringLiteral("#fbbf24"));
    m_cameraDirectionLabel->setStyleSheet(QStringLiteral(
        "QLabel {"
        " background: rgba(3, 8, 13, 225);"
        " color: #f8fafc;"
        " border: 2px solid %1;"
        " border-radius: 10px;"
        " padding: 8px 10px;"
        " font-family: 'DejaVu Sans Mono';"
        " font-size: 13px;"
        "}").arg(accent));

    m_cameraDirectionLabel->setText(QStringLiteral(
        "<div style='font-size:15px; font-weight:700; color:%1;'>%2</div>"
        "<div>%3 · %4</div>"
        "<div>Yaw %5° · Pitch %6°</div>"
        "<div>Forward [%7, %8, %9]</div>")
        .arg(accent,
             vertical.toHtmlEscaped(),
             cameraName.toHtmlEscaped(),
             mode.toHtmlEscaped())
        .arg(QString::number(yaw, 'f', 1),
             QString::number(pitch, 'f', 1),
             QString::number(fx, 'f', 2),
             QString::number(fy, 'f', 2),
             QString::number(fz, 'f', 2)));
    m_cameraDirectionLabel->setToolTip(QStringLiteral("Camera is facing %1").arg(compass));
    m_cameraDirectionLabel->adjustSize();

    const int margin = 14;
    const int maxWidth = std::max(260, (m_renderSurface ? m_renderSurface->width() : width()) - margin * 2);
    m_cameraDirectionLabel->setMaximumWidth(maxWidth);
    m_cameraDirectionLabel->move(margin, margin);
    m_cameraDirectionLabel->show();
    m_cameraDirectionLabel->raise();
}

QImage ViewportHostWidget::primitiveTexturePreview(int sceneIndex, int meshIndex, int primitiveIndex) const
{
    if (!m_runtime->engine() || sceneIndex < 0 || meshIndex < 0 || primitiveIndex < 0)
    {
        return {};
    }
    if (sceneIndex >= static_cast<int>(m_runtime->engine()->models.size()) || !m_runtime->engine()->models[static_cast<size_t>(sceneIndex)])
    {
        return {};
    }

    const auto& model = m_runtime->engine()->models[static_cast<size_t>(sceneIndex)];
    if (meshIndex >= static_cast<int>(model->meshes.size()))
    {
        return {};
    }

    const auto& mesh = model->meshes[static_cast<size_t>(meshIndex)];
    if (primitiveIndex >= static_cast<int>(mesh.primitives.size()) || !mesh.primitives[static_cast<size_t>(primitiveIndex)])
    {
        return {};
    }

    return mesh.primitives[static_cast<size_t>(primitiveIndex)]->texturePreviewImage;
}

QVector3D ViewportHostWidget::sceneItemBoundsSize(int sceneIndex) const
{
    if (!m_runtime->engine() || sceneIndex < 0 || sceneIndex >= static_cast<int>(m_runtime->engine()->models.size()))
    {
        return QVector3D(0.0f, 0.0f, 0.0f);
    }

    const auto& model = m_runtime->engine()->models[static_cast<size_t>(sceneIndex)];
    if (!model)
    {
        return QVector3D(0.0f, 0.0f, 0.0f);
    }

    const glm::vec3 size = glm::max(model->boundsMaxWorld - model->boundsMinWorld, glm::vec3(0.0f));
    return QVector3D(size.x, size.y, size.z);
}

QVector3D ViewportHostWidget::sceneItemBoundsCenter(int sceneIndex) const
{
    if (!m_runtime->engine() || sceneIndex < 0 || sceneIndex >= static_cast<int>(m_runtime->engine()->models.size()))
    {
        return QVector3D(0.0f, 0.0f, 0.0f);
    }

    const auto& model = m_runtime->engine()->models[static_cast<size_t>(sceneIndex)];
    if (!model)
    {
        return QVector3D(0.0f, 0.0f, 0.0f);
    }

    return QVector3D(model->boundsCenter.x, model->boundsCenter.y, model->boundsCenter.z);
}

QVector3D ViewportHostWidget::sceneItemBoundsMin(int sceneIndex) const
{
    if (!m_runtime->engine() || sceneIndex < 0 || sceneIndex >= static_cast<int>(m_runtime->engine()->models.size()))
    {
        return QVector3D(0.0f, 0.0f, 0.0f);
    }

    const auto& model = m_runtime->engine()->models[static_cast<size_t>(sceneIndex)];
    if (!model)
    {
        return QVector3D(0.0f, 0.0f, 0.0f);
    }

    return QVector3D(model->boundsMinWorld.x, model->boundsMinWorld.y, model->boundsMinWorld.z);
}

QVector3D ViewportHostWidget::sceneItemBoundsMax(int sceneIndex) const
{
    if (!m_runtime->engine() || sceneIndex < 0 || sceneIndex >= static_cast<int>(m_runtime->engine()->models.size()))
    {
        return QVector3D(0.0f, 0.0f, 0.0f);
    }

    const auto& model = m_runtime->engine()->models[static_cast<size_t>(sceneIndex)];
    if (!model)
    {
        return QVector3D(0.0f, 0.0f, 0.0f);
    }

    return QVector3D(model->boundsMaxWorld.x, model->boundsMaxWorld.y, model->boundsMaxWorld.z);
}

QString ViewportHostWidget::animationExecutionMode(int sceneIndex, int meshIndex, int primitiveIndex) const
{
    if (!m_runtime->engine() || sceneIndex < 0 || sceneIndex >= static_cast<int>(m_runtime->engine()->models.size()) || !m_runtime->engine()->models[static_cast<size_t>(sceneIndex)])
    {
        return QStringLiteral("Static");
    }

    const auto& model = m_runtime->engine()->models[static_cast<size_t>(sceneIndex)];
    const auto classifyPrimitive = [](const Primitive* primitive) -> QString
    {
        if (!primitive)
        {
            return QStringLiteral("Static");
        }
        if (primitive->gpuSkinningEnabled && primitive->skinJointCount > 0)
        {
            return QStringLiteral("GPU skinning");
        }
        if (primitive->skinJointCount > 0)
        {
            return QStringLiteral("CPU skinning");
        }
        return QStringLiteral("Static");
    };

    if (meshIndex >= 0 && primitiveIndex >= 0)
    {
        if (meshIndex >= static_cast<int>(model->meshes.size()))
        {
            return QStringLiteral("Static");
        }
        const auto& mesh = model->meshes[static_cast<size_t>(meshIndex)];
        if (primitiveIndex >= static_cast<int>(mesh.primitives.size()))
        {
            return QStringLiteral("Static");
        }
        return classifyPrimitive(mesh.primitives[static_cast<size_t>(primitiveIndex)].get());
    }

    bool sawCpu = false;
    for (const auto& mesh : model->meshes)
    {
        for (const auto& primitive : mesh.primitives)
        {
            const QString mode = classifyPrimitive(primitive.get());
            if (mode == QStringLiteral("GPU skinning"))
            {
                return mode;
            }
            if (mode == QStringLiteral("CPU skinning"))
            {
                sawCpu = true;
            }
        }
    }

    if (sawCpu)
    {
        return QStringLiteral("CPU skinning");
    }
    if (!model->animationClips.empty())
    {
        return QStringLiteral("Animated");
    }
    return QStringLiteral("Static");
}

QString ViewportHostWidget::primitiveCullMode(int sceneIndex, int meshIndex, int primitiveIndex) const
{
    if (!m_runtime->engine() || sceneIndex < 0 || meshIndex < 0 || primitiveIndex < 0)
    {
        return QStringLiteral("back");
    }
    if (sceneIndex >= static_cast<int>(m_runtime->engine()->models.size()) || !m_runtime->engine()->models[static_cast<size_t>(sceneIndex)])
    {
        return QStringLiteral("back");
    }
    const auto& model = m_runtime->engine()->models[static_cast<size_t>(sceneIndex)];
    if (meshIndex >= static_cast<int>(model->meshes.size()))
    {
        return QStringLiteral("back");
    }
    const auto& mesh = model->meshes[static_cast<size_t>(meshIndex)];
    if (primitiveIndex >= static_cast<int>(mesh.primitives.size()) || !mesh.primitives[static_cast<size_t>(primitiveIndex)])
    {
        return QStringLiteral("back");
    }

    switch (mesh.primitives[static_cast<size_t>(primitiveIndex)]->cullMode)
    {
    case PrimitiveCullMode::Back:
        return QStringLiteral("back");
    case PrimitiveCullMode::Disabled:
        return QStringLiteral("none");
    case PrimitiveCullMode::Front:
        return QStringLiteral("front");
    }
    return QStringLiteral("back");
}

QString ViewportHostWidget::sceneItemCullMode(int sceneIndex) const
{
    if (!m_runtime->engine() || sceneIndex < 0 ||
        sceneIndex >= static_cast<int>(m_runtime->engine()->models.size()) ||
        !m_runtime->engine()->models[static_cast<size_t>(sceneIndex)])
    {
        return QStringLiteral("back");
    }

    const auto& model = m_runtime->engine()->models[static_cast<size_t>(sceneIndex)];
    QString firstMode;
    bool sawPrimitive = false;
    for (size_t meshIndex = 0; meshIndex < model->meshes.size(); ++meshIndex)
    {
        const auto& mesh = model->meshes[meshIndex];
        for (size_t primitiveIndex = 0; primitiveIndex < mesh.primitives.size(); ++primitiveIndex)
        {
            if (!mesh.primitives[primitiveIndex])
            {
                continue;
            }
            const QString mode = primitiveCullMode(sceneIndex,
                                                   static_cast<int>(meshIndex),
                                                   static_cast<int>(primitiveIndex));
            if (!sawPrimitive)
            {
                firstMode = mode;
                sawPrimitive = true;
            }
            else if (mode != firstMode)
            {
                return QStringLiteral("mixed");
            }
        }
    }
    return sawPrimitive ? firstMode : QStringLiteral("back");
}

bool ViewportHostWidget::primitiveForceAlphaOne(int sceneIndex, int meshIndex, int primitiveIndex) const
{
    if (!m_runtime->engine() || sceneIndex < 0 || meshIndex < 0 || primitiveIndex < 0)
    {
        return false;
    }
    if (sceneIndex >= static_cast<int>(m_runtime->engine()->models.size()) || !m_runtime->engine()->models[static_cast<size_t>(sceneIndex)])
    {
        return false;
    }
    const auto& model = m_runtime->engine()->models[static_cast<size_t>(sceneIndex)];
    if (meshIndex >= static_cast<int>(model->meshes.size()))
    {
        return false;
    }
    const auto& mesh = model->meshes[static_cast<size_t>(meshIndex)];
    if (primitiveIndex >= static_cast<int>(mesh.primitives.size()) || !mesh.primitives[static_cast<size_t>(primitiveIndex)])
    {
        return false;
    }
    return mesh.primitives[static_cast<size_t>(primitiveIndex)]->forceAlphaOne;
}

QStringList ViewportHostWidget::animationClipNames(int sceneIndex) const
{
    QStringList clips;
    if (!m_runtime->engine() || sceneIndex < 0 || sceneIndex >= static_cast<int>(m_runtime->engine()->models.size()) || !m_runtime->engine()->models[static_cast<size_t>(sceneIndex)])
    {
        return clips;
    }

    for (const auto& clip : m_runtime->engine()->models[static_cast<size_t>(sceneIndex)]->animationClips)
    {
        clips.push_back(QString::fromStdString(clip.name));
    }
    return clips;
}

bool ViewportHostWidget::hasSceneLight() const
{
    return m_sceneLight.exists;
}

ViewportHostWidget::SceneLight ViewportHostWidget::sceneLight() const
{
    return m_sceneLight;
}

QVector3D ViewportHostWidget::cameraPosition() const
{
    return m_cameraController->cameraPosition();
}

QVector3D ViewportHostWidget::cameraRotation() const
{
    return m_cameraController->cameraRotation();
}

float ViewportHostWidget::cameraSpeed() const
{
    return m_cameraController->cameraSpeed();
}

QString ViewportHostWidget::renderPath() const
{
    return m_runtime->use2DPipeline() ? QStringLiteral("flat2d") : QStringLiteral("forward3d");
}

bool ViewportHostWidget::meshConsolidationEnabled() const
{
    return m_sceneController->meshConsolidationEnabled();
}

ViewportHostWidget::PerformanceMetrics ViewportHostWidget::performanceMetrics() const
{
    PerformanceMetrics metrics;
    metrics.renderIntervalMs = m_renderTimer.interval();
    metrics.renderTimerActive = m_renderTimer.isActive();
    metrics.viewportWidth = m_renderSurface ? m_renderSurface->width() : width();
    metrics.viewportHeight = m_renderSurface ? m_renderSurface->height() : height();
    
    if (m_initialized && m_runtime && m_runtime->display())
    {
        metrics.currentFps = m_runtime->display()->getCurrentFps();
    }
    
    return metrics;
}

QJsonObject ViewportHostWidget::motionDebugOverlayOptionsJson() const
{
    const MotionDebugOverlayOptions options = m_motionDebugOverlayOptions;
    return QJsonObject{
        {QStringLiteral("enabled"), options.enabled},
        {QStringLiteral("showTargetMarkers"), options.showTargetMarkers},
        {QStringLiteral("showVelocityVector"), options.showVelocityVector},
        {QStringLiteral("showCameraToTargetLine"), options.showCameraToTargetLine},
        {QStringLiteral("showScreenCenterCrosshair"), options.showScreenCenterCrosshair},
        {QStringLiteral("showMotionTrail"), options.showMotionTrail},
        {QStringLiteral("showRawTrail"), options.showRawTrail},
        {QStringLiteral("trailFrames"), options.trailFrames},
        {QStringLiteral("velocityScale"), options.velocityScale}
    };
}

void ViewportHostWidget::setMotionDebugOverlayOptions(const MotionDebugOverlayOptions& options)
{
    m_motionDebugOverlayOptions.enabled = options.enabled;
    m_motionDebugOverlayOptions.showTargetMarkers = options.showTargetMarkers;
    m_motionDebugOverlayOptions.showVelocityVector = options.showVelocityVector;
    m_motionDebugOverlayOptions.showCameraToTargetLine = options.showCameraToTargetLine;
    m_motionDebugOverlayOptions.showScreenCenterCrosshair = options.showScreenCenterCrosshair;
    m_motionDebugOverlayOptions.showMotionTrail = options.showMotionTrail;
    m_motionDebugOverlayOptions.showRawTrail = options.showRawTrail;
    m_motionDebugOverlayOptions.trailFrames = std::clamp(options.trailFrames, 2, 240);
    m_motionDebugOverlayOptions.velocityScale = std::clamp(options.velocityScale, 0.01f, 10.0f);

    if (!m_runtime || !m_runtime->display() || m_motionDebugOverlayOptions.enabled)
    {
        return;
    }
    m_runtime->display()->clearCustomOverlayBitmap();
}

void ViewportHostWidget::enableCharacterControl(int sceneIndex, bool enabled)
{
    setCharacterControlState(sceneIndex, enabled, true);
}

void ViewportHostWidget::selectCharacterControlOwner(int sceneIndex)
{
    setCharacterControlState(sceneIndex, true, false);
}

void ViewportHostWidget::setCharacterControlState(int sceneIndex, bool enabled, bool repositionForCharacterMode)
{
    qDebug() << "[ViewportHost][DEBUG] setCharacterControlState called: sceneIndex=" << sceneIndex 
             << " enabled=" << enabled << " reposition=" << repositionForCharacterMode;
    
    if (!m_runtime->engine() || sceneIndex < 0 || sceneIndex >= static_cast<int>(m_runtime->engine()->models.size()))
    {
        qDebug() << "[ViewportHost][DEBUG] Invalid parameters or engine not ready";
        return;
    }
    
    auto& model = m_runtime->engine()->models[sceneIndex];
    if (!model)
    {
        qDebug() << "[ViewportHost][DEBUG] Model at sceneIndex" << sceneIndex << "is null";
        return;
    }
    
    if (enabled)
    {
        // Single-owner policy: exactly one controllable model at a time.
        for (int i = 0; i < static_cast<int>(m_runtime->engine()->models.size()); ++i)
        {
            if (i == sceneIndex)
            {
                continue;
            }
            auto& other = m_runtime->engine()->models[static_cast<size_t>(i)];
            if (!other || !other->character.isControllable)
            {
                continue;
            }
            other->character.isControllable = false;
            other->character.velocity = glm::vec3(0.0f);
            other->character.inputDir = glm::vec3(0.0f);
            other->character.isGrounded = false;
            other->character.jumpRequested = false;
            other->character.keyW = false;
            other->character.keyA = false;
            other->character.keyS = false;
            other->character.keyD = false;
        }
    }

    qDebug() << "[ViewportHost][DEBUG] Setting character.isControllable =" << enabled
             << " for model" << sceneIndex << "(was:" << model->character.isControllable << ")";
    model->character.isControllable = enabled;
    
    // Character controllability controls input ownership only. Physics coupling
    // is explicit scene state; the default TPS setup uses the arcade controller
    // because imported environments do not necessarily have collision bodies.
    
    // Only the explicit editor character-control action should reposition the model.
    if (enabled && repositionForCharacterMode)
    {
        glm::vec3 pos = glm::vec3(model->worldTransform[3]);
        pos.y = 5.0f;  // Start 5 units above ground (closer drop)
        model->worldTransform[3] = glm::vec4(pos, 1.0f);
        model->recomputeBounds();
        model->character.velocity = glm::vec3(0.0f);
        model->character.isGrounded = false;
        qDebug() << "[ViewportHost] Character teleported to Y=5 to drop by gravity";
    }
    else if (!enabled)
    {
        model->character.velocity = glm::vec3(0.0f);
        model->character.inputDir = glm::vec3(0.0f);
        model->character.isGrounded = false;
        model->character.jumpRequested = false;
        model->character.keyW = false;
        model->character.keyA = false;
        model->character.keyS = false;
        model->character.keyD = false;
    }
    
    // Character controllability should not directly mutate camera follow/mode state.
    syncViewportSelectorChoices();
    if (m_cameraChangedCallback)
    {
        m_cameraChangedCallback();
    }

    qDebug() << "[ViewportHost] Character control" << (enabled ? "enabled" : "disabled") << "for model" << sceneIndex;
}

bool ViewportHostWidget::isCharacterControlEnabled(int sceneIndex) const
{
    if (!m_runtime->engine() || sceneIndex < 0 || sceneIndex >= static_cast<int>(m_runtime->engine()->models.size()))
    {
        return false;
    }
    
    const auto& model = m_runtime->engine()->models[sceneIndex];
    if (!model)
    {
        return false;
    }
    
    return model->character.isControllable;
}

bool ViewportHostWidget::injectCharacterInput(int sceneIndex,
                                              bool keyW,
                                              bool keyA,
                                              bool keyS,
                                              bool keyD,
                                              bool jumpRequested,
                                              int durationMs)
{
    if (!m_runtime || !m_runtime->engine() || !m_runtime->display())
    {
        return false;
    }
    if (sceneIndex < 0 || sceneIndex >= static_cast<int>(m_runtime->engine()->models.size()) ||
        !m_runtime->engine()->models[static_cast<size_t>(sceneIndex)])
    {
        return false;
    }

    Model* model = m_runtime->engine()->models[static_cast<size_t>(sceneIndex)].get();
    if (!model)
    {
        return false;
    }

    // Ensure selected owner receives routed character input, but avoid redundant churn.
    if (!model->character.isControllable)
    {
        selectCharacterControlOwner(sceneIndex);
    }

    Camera* activeCamera = m_runtime->camera();
    const bool activeCameraFollowsSelection =
        activeCamera &&
        activeCamera->isFollowModeEnabled() &&
        activeCamera->getFollowSceneIndex() == sceneIndex &&
        !activeCamera->isFreeFlyCamera();

    if (!activeCameraFollowsSelection)
    {
        const int followCameraIndex = ensureFollowCamera(sceneIndex, 5.0f, 0.0f, 20.0f);
        if (followCameraIndex >= 0)
        {
            setActiveCamera(followCameraIndex);
        }
        setFreeFlyCameraEnabled(false);
    }

    // Keep controllable character framing centered on the follow anchor.
    if (Camera* camera = m_runtime->camera())
    {
        if (camera->isFollowModeEnabled() && camera->getFollowSceneIndex() == sceneIndex)
        {
            FollowSettings settings = camera->getFollowSettings();
            if (glm::length(settings.targetOffset) > 1e-5f)
            {
                settings.targetOffset = glm::vec3(0.0f);
                camera->setFollowTarget(sceneIndex, settings);
            }
        }
    }

    InputRouter* inputRouter = m_runtime->getInputRouter();
    if (!inputRouter)
    {
        return false;
    }

    const int clampedDurationMs = std::clamp(durationMs, 16, 5000);
    inputRouter->setSimulatedInput(
        std::array<bool, 6>{keyW, keyA, keyS, keyD, false, false},
        jumpRequested,
        static_cast<float>(clampedDurationMs) / 1000.0f);
    return true;
}

bool ViewportHostWidget::playCharacterInputPattern(int sceneIndex,
                                                   const QString& pattern,
                                                   int stepDurationMs,
                                                   int steps,
                                                   bool includeJump)
{
    static const QStringList kCirclePattern = {
        QStringLiteral("W"), QStringLiteral("WD"), QStringLiteral("D"), QStringLiteral("SD"),
        QStringLiteral("S"), QStringLiteral("SA"), QStringLiteral("A"), QStringLiteral("WA")
    };
    static const QStringList kFigure8Pattern = {
        QStringLiteral("W"), QStringLiteral("WD"), QStringLiteral("D"), QStringLiteral("SD"),
        QStringLiteral("S"), QStringLiteral("SA"), QStringLiteral("A"), QStringLiteral("WA"),
        QStringLiteral("W"), QStringLiteral("WA"), QStringLiteral("A"), QStringLiteral("SA"),
        QStringLiteral("S"), QStringLiteral("SD"), QStringLiteral("D"), QStringLiteral("WD")
    };
    static const QStringList kStrafePattern = {
        QStringLiteral("A"), QStringLiteral("A"), QStringLiteral("D"), QStringLiteral("D")
    };

    const QString normalizedPattern = pattern.trimmed().toLower();
    const QStringList* moves = nullptr;
    if (normalizedPattern == QStringLiteral("circle"))
    {
        moves = &kCirclePattern;
    }
    else if (normalizedPattern == QStringLiteral("figure8") || normalizedPattern == QStringLiteral("figure_8"))
    {
        moves = &kFigure8Pattern;
    }
    else if (normalizedPattern == QStringLiteral("strafe"))
    {
        moves = &kStrafePattern;
    }
    else
    {
        return false;
    }

    const int clampedSteps = std::clamp(steps, 1, 512);
    const int clampedStepMs = std::clamp(stepDurationMs, 16, 1000);
    for (int i = 0; i < clampedSteps; ++i)
    {
        const QString move = moves->at(i % moves->size());
        const auto moveFlags = decodeMovePattern(move);
        const bool jumpNow = includeJump && (i == clampedSteps / 2);
        QTimer::singleShot(i * clampedStepMs, this, [this, sceneIndex, moveFlags, jumpNow, clampedStepMs]()
        {
            injectCharacterInput(sceneIndex,
                                 moveFlags[0],
                                 moveFlags[1],
                                 moveFlags[2],
                                 moveFlags[3],
                                 jumpNow,
                                 clampedStepMs);
        });
    }
    return true;
}

bool ViewportHostWidget::bootstrapThirdPersonShooter(bool force)
{
    if (!m_runtime || !m_runtime->engine() || !m_runtime->display())
    {
        return false;
    }

    if (m_tpsBootstrapApplied && !force)
    {
        return true;
    }

    const auto& entries = m_sceneController->loadedEntries();
    if (entries.isEmpty())
    {
        return false;
    }

    int nathanIndex = -1;
    int environmentIndex = -1;
    for (int i = 0; i < entries.size(); ++i)
    {
        if (nathanIndex < 0 && pathLooksLikeNathanCharacter(entries[i].sourcePath))
        {
            nathanIndex = i;
        }
        if (environmentIndex < 0 && pathLooksLikeEnvironmentScene(entries[i].sourcePath))
        {
            environmentIndex = i;
        }
    }

    if (nathanIndex < 0 || environmentIndex < 0)
    {
        return false;
    }

    if (nathanIndex >= static_cast<int>(m_runtime->engine()->models.size()) ||
        !m_runtime->engine()->models[static_cast<size_t>(nathanIndex)])
    {
        return false;
    }

    Model* nathan = m_runtime->engine()->models[static_cast<size_t>(nathanIndex)].get();
    if (!nathan)
    {
        return false;
    }

    // Gameplay convention: Motive editor/world uses Y-up with the XZ plane at Y=0
    // as the authoritative walkable ground. Imported environments may have large
    // visual bounds, but TPS character placement should be deterministic.
    const float worldGroundY = 0.0f;

    if (environmentIndex >= 0 &&
        environmentIndex < static_cast<int>(m_runtime->engine()->models.size()) &&
        m_runtime->engine()->models[static_cast<size_t>(environmentIndex)])
    {
        Model* environment = m_runtime->engine()->models[static_cast<size_t>(environmentIndex)].get();
        const float environmentBottomDelta = worldGroundY - environment->boundsMinWorld.y;
        if (std::isfinite(environmentBottomDelta) && std::abs(environmentBottomDelta) > 0.0001f &&
            environmentIndex < entries.size())
        {
            const auto& environmentEntry = entries[environmentIndex];
            m_sceneController->updateSceneItemTransform(
                environmentIndex,
                QVector3D(environmentEntry.translation.x(),
                          environmentEntry.translation.y() + environmentBottomDelta,
                          environmentEntry.translation.z()),
                environmentEntry.rotation,
                environmentEntry.scale);
        }
    }

    auto entriesBeforePlacement = m_sceneController->loadedEntries();
    if (nathanIndex < entriesBeforePlacement.size())
    {
        const auto& entry = entriesBeforePlacement[nathanIndex];
        m_sceneController->updateSceneItemTransform(nathanIndex,
                                                    entry.translation,
                                                    QVector3D(0.0f, 0.0f, 0.0f),
                                                    entry.scale);
        nathan = m_runtime->engine()->models[static_cast<size_t>(nathanIndex)].get();
        if (nathan)
        {
            const float bottomDelta = worldGroundY - nathan->boundsMinWorld.y;
            if (std::isfinite(bottomDelta) && std::abs(bottomDelta) > 0.0001f)
            {
                const auto entriesAfterRotation = m_sceneController->loadedEntries();
                if (nathanIndex < entriesAfterRotation.size())
                {
                    const QVector3D translation = entriesAfterRotation[nathanIndex].translation;
                    m_sceneController->updateSceneItemTransform(
                        nathanIndex,
                        QVector3D(translation.x(), translation.y() + bottomDelta, translation.z()),
                        QVector3D(0.0f, 0.0f, 0.0f),
                        entriesAfterRotation[nathanIndex].scale);
                    nathan = m_runtime->engine()->models[static_cast<size_t>(nathanIndex)].get();
                }
            }
        }
    }

    // Ensure ownership + arcade controller state are configured for WASD character control.
    setCharacterControlState(nathanIndex, true, false);
    updateSceneItemAnimationPhysicsCoupling(nathanIndex, QStringLiteral("AnimationOnly"));
    updateSceneItemPhysicsGravity(nathanIndex, true, QVector3D(0.0f, 0.0f, 0.0f));
    updateSceneItemCharacterTurnResponsiveness(nathanIndex, 4.0f);

    // Reasonable gameplay defaults.
    nathan->character.velocity = glm::vec3(0.0f);
    nathan->character.inputDir = glm::vec3(0.0f);
    // The rendered feet are aligned to worldGroundY, but Nathan's imported
    // transform origin sits around the body center. Clamp the controller origin
    // at its current Y so the visual bottom remains on the XZ ground plane.
    nathan->character.groundHeight = nathan->worldTransform[3].y;
    nathan->character.isGrounded = true;
    nathan->character.wasGroundedLastFrame = true;
    nathan->character.jumpRequested = false;
    nathan->character.jumpPhase = Model::CharacterController::JumpPhase::None;
    nathan->character.jumpPhaseTimer = 0.0f;
    nathan->character.currentAnimState = Model::CharacterController::AnimState::Idle;
    nathan->character.keyW = false;
    nathan->character.keyA = false;
    nathan->character.keyS = false;
    nathan->character.keyD = false;
    nathan->character.comeToRestTimer = 0.0f;
    nathan->character.moveIntentGraceTimer = 0.0f;
    nathan->character.pendingRestPointLatch = false;
    nathan->character.moveSpeed = 3.2f;
    nathan->character.runSpeedThreshold = 4.5f;
    nathan->character.walkSpeedThreshold = 0.08f;
    if (InputRouter* inputRouter = m_runtime->getInputRouter())
    {
        inputRouter->clearInput();
    }

    // Prefer a walk-forward clip when present.
    QString walkClip;
    const QStringList clips = animationClipNames(nathanIndex);
    for (const QString& clip : clips)
    {
        const QString lower = clip.toLower();
        if (lower.contains(QStringLiteral("walk")) && !lower.contains(QStringLiteral("back")))
        {
            walkClip = clip;
            break;
        }
    }
    if (walkClip.isEmpty() && !clips.isEmpty())
    {
        walkClip = clips.front();
    }
    if (!walkClip.isEmpty())
    {
        updateSceneItemAnimationState(nathanIndex, walkClip, true, true, 1.0f);
    }

    // Ensure camera follows Nathan and WASD routes to character instead of free-fly.
    const int followCameraIndex = ensureFollowCamera(nathanIndex, 4.8f, 5.0f, 17.0f);
    if (followCameraIndex >= 0)
    {
        setActiveCamera(followCameraIndex);
    }
    setFreeFlyCameraEnabled(false);

    m_tpsBootstrapApplied = true;
    m_tpsBootstrapPending = false;
    qDebug() << "[ViewportHost] TPS bootstrap applied. Nathan index =" << nathanIndex
             << " environment index =" << environmentIndex
             << " follow camera index =" << followCameraIndex;
    return true;
}

QJsonObject ViewportHostWidget::bootstrapThirdPersonShooterReport(bool force)
{
    QJsonObject stateBefore = thirdPersonShooterStateJson();
    const bool applied = bootstrapThirdPersonShooter(force);
    QJsonObject stateAfter = thirdPersonShooterStateJson();

    QJsonArray reasons;
    if (!stateBefore.value(QStringLiteral("runtimeReady")).toBool(false))
    {
        reasons.append(QStringLiteral("runtime_not_ready"));
    }
    if (!stateBefore.value(QStringLiteral("hasNathan")).toBool(false))
    {
        reasons.append(QStringLiteral("nathan_not_loaded"));
    }
    if (!stateBefore.value(QStringLiteral("hasEnvironment")).toBool(false))
    {
        reasons.append(QStringLiteral("environment_not_loaded"));
    }
    if (!applied && stateBefore.value(QStringLiteral("runtimeReady")).toBool(false) &&
        stateBefore.value(QStringLiteral("hasNathan")).toBool(false) &&
        stateBefore.value(QStringLiteral("hasEnvironment")).toBool(false))
    {
        reasons.append(QStringLiteral("bootstrap_apply_failed"));
    }

    stateAfter.insert(QStringLiteral("ok"), true);
    stateAfter.insert(QStringLiteral("force"), force);
    stateAfter.insert(QStringLiteral("applied"), applied);
    stateAfter.insert(QStringLiteral("reasons"), reasons);
    return stateAfter;
}

QJsonObject ViewportHostWidget::thirdPersonShooterStateJson() const
{
    QJsonObject state;
    const bool runtimeReady = m_runtime && m_runtime->engine() && m_runtime->display();
    state.insert(QStringLiteral("runtimeReady"), runtimeReady);
    state.insert(QStringLiteral("bootstrapPending"), m_tpsBootstrapPending);
    state.insert(QStringLiteral("bootstrapApplied"), m_tpsBootstrapApplied);

    int nathanIndex = -1;
    int environmentIndex = -1;
    const auto& entries = m_sceneController->loadedEntries();
    for (int i = 0; i < entries.size(); ++i)
    {
        if (nathanIndex < 0 && pathLooksLikeNathanCharacter(entries[i].sourcePath))
        {
            nathanIndex = i;
        }
        if (environmentIndex < 0 && pathLooksLikeEnvironmentScene(entries[i].sourcePath))
        {
            environmentIndex = i;
        }
    }
    state.insert(QStringLiteral("nathanIndex"), nathanIndex);
    state.insert(QStringLiteral("environmentIndex"), environmentIndex);
    state.insert(QStringLiteral("hasNathan"), nathanIndex >= 0);
    state.insert(QStringLiteral("hasEnvironment"), environmentIndex >= 0);

    bool nathanControllable = false;
    if (nathanIndex >= 0)
    {
        nathanControllable = isCharacterControlEnabled(nathanIndex);
    }
    state.insert(QStringLiteral("nathanControllable"), nathanControllable);

    const int followCameraIndex =
        runtimeReady ? findFollowCameraIndexForScene(m_runtime->display(), nathanIndex) : -1;
    state.insert(QStringLiteral("followCameraIndex"), followCameraIndex);
    state.insert(QStringLiteral("freeFly"), isFreeFlyCameraEnabled());
    state.insert(QStringLiteral("activeCameraIndex"), activeCameraIndex());
    state.insert(QStringLiteral("activeCameraId"), activeCameraId());
    return state;
}

motive::IPhysicsBody* ViewportHostWidget::getPhysicsBodyForSceneItem(int sceneIndex) const
{
    if (!m_runtime || !m_runtime->engine() || sceneIndex < 0 || sceneIndex >= static_cast<int>(m_runtime->engine()->models.size()))
    {
        return nullptr;
    }
    
    const auto& model = m_runtime->engine()->models[sceneIndex];
    if (!model)
    {
        return nullptr;
    }
    
    return model->getPhysicsBody();
}

void ViewportHostWidget::setFreeFlyCameraEnabled(bool enabled)
{
    qDebug() << "[ViewportHost] Free fly camera" << (enabled ? "enabled" : "disabled");
    
    if (!m_runtime->camera())
    {
        return;
    }
    
    Camera* cam = m_runtime->camera();
    
    if (enabled)
    {
        // Free fly mode: keep follow target/settings so toggling back can resume follow.
        cam->setMode(CameraMode::FreeFly);
        // No character target in free fly mode
    }
    else
    {
        // Character follow mode: if a valid follow target already exists, resume it.
        if (cam->isFollowModeEnabled() && cam->getFollowSceneIndex() >= 0)
        {
            cam->setMode(CameraMode::CharacterFollow);
            return;
        }

        // Otherwise, find controllable character and set up follow mode.
        // Find controllable character and set up follow
        for (auto& model : m_runtime->engine()->models)
        {
            if (model && model->character.isControllable)
            {
                // Find scene index for this model
                int sceneIndex = -1;
                for (int i = 0; i < static_cast<int>(m_runtime->engine()->models.size()); ++i)
                {
                    if (m_runtime->engine()->models[i].get() == model.get())
                    {
                        sceneIndex = i;
                        break;
                    }
                }
                
                if (sceneIndex >= 0)
                {
                    // Set up follow mode with proper settings
                    FollowSettings settings = makeFollowSettings(
                        0.0f,
                        0.35f,  // Slightly up (~20 degrees)
                        3.0f,
                        followcam::kDefaultSmoothSpeed);
                    cam->setFollowTarget(sceneIndex, settings);
                    cam->setMode(CameraMode::CharacterFollow);
                    
                    // Position camera at initial offset
                    glm::vec3 charPos = model->getCharacterPosition();
                    glm::vec3 camPos = charPos;
                    camPos.z += settings.distance;
                    camPos.y += sin(settings.relativePitch) * settings.distance;
                    cam->cameraPos = camPos;
                    
                    // Set initial rotation to look at character
                    glm::vec3 lookTarget = charPos + glm::vec3(0.0f, 0.8f, 0.0f);
                    glm::vec3 front = glm::normalize(lookTarget - camPos);
                    cam->setEulerRotation(detail::cameraRotationForDirection(front));
                    cam->update(0);
                    
                    qDebug() << "[ViewportHost] Camera positioned behind character at:" << camPos.x << camPos.y << camPos.z;
                    break;
                }
            }
        }
    }
    
}

bool ViewportHostWidget::isFreeFlyCameraEnabled() const
{
    Camera* cam = m_runtime->camera();
    return cam && cam->getMode() == CameraMode::FreeFly;
}

QList<ViewportHostWidget::CameraConfig> ViewportHostWidget::cameraConfigs() const
{
    QList<CameraConfig> configs;
    
    if (!m_runtime->display()) {
        return m_pendingCameraConfigs;
    }
    
    for (Camera* camera : m_runtime->display()->cameras) {
        if (!camera) continue;
        if (camera->getCameraId().empty()) {
            camera->setCameraId(makeCameraId().toStdString());
        }
        
        CameraConfig config;
        config.id = QString::fromStdString(camera->getCameraId());
        config.name = QString::fromStdString(camera->getCameraName());
        config.mode = cameraModeToString(camera->getMode());
        
        // Check if this is a follow camera
        if (camera->isFollowModeEnabled() && camera->getFollowTargetIndex() >= 0) {
            config.type = CameraConfig::Type::Follow;
            config.followTargetIndex = camera->getFollowSceneIndex();
            config.position = QVector3D(camera->cameraPos.x, camera->cameraPos.y, camera->cameraPos.z);
            const glm::vec2 rotation = camera->getEulerRotation();
            config.rotation = QVector3D(
                glm::degrees(rotation.x),
                glm::degrees(rotation.y),
                0.0f
            );
            
            const FollowSettings& fs = camera->getFollowSettings();
            config.followDistance = fs.distance;
            config.followYaw = glm::degrees(fs.relativeYaw);
            config.followPitch = glm::degrees(fs.relativePitch);
            config.followSmoothSpeed = fs.smoothSpeed;
            config.followTargetOffset = QVector3D(fs.targetOffset.x, fs.targetOffset.y, fs.targetOffset.z);
            config.freeFly = camera->isFreeFlyCamera();
            config.invertHorizontalDrag = camera->isHorizontalDragInverted();
        } else {
            config.type = CameraConfig::Type::Free;
            config.position = QVector3D(camera->cameraPos.x, camera->cameraPos.y, camera->cameraPos.z);
            // Convert camera rotation (radians) to degrees
            const glm::vec2 rotation = camera->getEulerRotation();
            config.rotation = QVector3D(
                glm::degrees(rotation.x),  // yaw
                glm::degrees(rotation.y),  // pitch
                0.0f
            );
            config.freeFly = camera->isFreeFlyCamera();
            config.invertHorizontalDrag = camera->isHorizontalDragInverted();
        }
        config.nearClip = camera->getPerspectiveNear();
        config.farClip = camera->getPerspectiveFar();
        
        configs.append(config);
    }
    
    return configs;
}

bool ViewportHostWidget::normalizeSceneScaleForMeters(float targetCharacterRadius)
{
    if (!m_runtime->engine() || !m_runtime->display())
    {
        return false;
    }

    if (targetCharacterRadius <= 0.0f || !std::isfinite(targetCharacterRadius))
    {
        return false;
    }

    Model* referenceModel = firstControllableModel(m_runtime->engine());
    if (!referenceModel)
    {
        for (auto& model : m_runtime->engine()->models)
        {
            if (model)
            {
                referenceModel = model.get();
                break;
            }
        }
    }
    if (!referenceModel || !std::isfinite(referenceModel->boundsRadius) || referenceModel->boundsRadius <= 1e-5f)
    {
        return false;
    }

    const float currentRadius = referenceModel->boundsRadius;
    float scaleFactor = targetCharacterRadius / currentRadius;
    if (!std::isfinite(scaleFactor))
    {
        return false;
    }

    scaleFactor = std::clamp(scaleFactor, 0.1f, 100.0f);
    if (scaleFactor > 0.8f && scaleFactor < 1.25f)
    {
        return false;
    }

    auto entries = m_sceneController->loadedEntries();
    if (entries.isEmpty())
    {
        return false;
    }

    for (int i = 0; i < entries.size(); ++i)
    {
        entries[i].translation *= scaleFactor;
        entries[i].scale *= scaleFactor;
        m_sceneController->updateSceneItemTransform(i, entries[i].translation, entries[i].rotation, entries[i].scale);
    }

    QList<CameraConfig> configs = cameraConfigs();
    for (auto& config : configs)
    {
        config.position *= scaleFactor;
        if (config.isFollowCamera())
        {
            config.followDistance = std::max(followcam::kMinDistance, config.followDistance * scaleFactor);
        }
        config.nearClip = std::max(0.001f, config.nearClip * scaleFactor);
        config.farClip = std::max(config.nearClip + 0.001f, config.farClip * scaleFactor);
    }
    setCameraConfigs(configs);

    m_orbitDistance = std::max(followcam::kMinDistance, m_orbitDistance * scaleFactor);

    qDebug() << "[ViewportHost] Scene scale normalized for meter-like units."
             << "factor" << scaleFactor
             << "referenceRadiusBefore" << currentRadius
             << "targetRadius" << targetCharacterRadius;
    return true;
}

ViewportHostWidget::ViewportLayout ViewportHostWidget::viewportLayout() const
{
    return normalizedViewportLayout(m_viewportLayout);
}

void ViewportHostWidget::setViewportLayout(const ViewportLayout& layout)
{
    const ViewportLayout normalized = normalizedViewportLayout(layout);
    if (m_viewportLayout.count == normalized.count && m_viewportLayout.cameraIds == normalized.cameraIds)
    {
        return;
    }

    m_viewportLayout = normalized;
    updateViewportLayout();
    syncViewportSelectorChoices();
    layoutViewportSelectors();
    m_focusedViewportIndex = std::clamp(m_focusedViewportIndex, 0, std::max(0, viewportCount() - 1));
    updateViewportBorders();

    if (m_cameraChangedCallback)
    {
        m_cameraChangedCallback();
    }

    syncViewportSelectorChoices();
}

void ViewportHostWidget::setViewportCount(int count)
{
    ViewportLayout layout = viewportLayout();
    layout.count = count;
    setViewportLayout(layout);
}

int ViewportHostWidget::viewportCount() const
{
    return normalizedViewportLayout(m_viewportLayout).count;
}

int ViewportHostWidget::focusedViewportIndex() const
{
    return std::clamp(m_focusedViewportIndex, 0, std::max(0, viewportCount() - 1));
}

QString ViewportHostWidget::focusedViewportCameraId() const
{
    return cameraIdForViewportIndex(focusedViewportIndex());
}

QStringList ViewportHostWidget::viewportCameraIds() const
{
    return normalizedViewportLayout(m_viewportLayout).cameraIds;
}

// Helper function for creating follow cameras with full settings
static Camera* createFollowCameraInternal(Display* display, Engine* engine, int sceneIndex, 
                                           float distance, float yaw, float pitch, 
                                           float smoothSpeed, const QVector3D& targetOffset)
{
    if (!engine || sceneIndex < 0) {
        qDebug() << "[ViewportHost] Cannot create follow camera (internal): missing engine or invalid scene index" << sceneIndex;
        return nullptr;
    }
    
    if (!display) {
        qDebug() << "[ViewportHost] Cannot create follow camera (internal): display not initialized";
        return nullptr;
    }
    
    // Build a unique name for this follow camera
    QString cameraName = QStringLiteral("Follow Cam (Scene %1)").arg(sceneIndex);
    std::string cameraNameStd = cameraName.toStdString();
    
    // Target may be unavailable during restore; create the camera anyway and let follow resolve later.
    Model* targetModel = (sceneIndex < static_cast<int>(engine->models.size()))
        ? engine->models[static_cast<size_t>(sceneIndex)].get()
        : nullptr;
    
    distance = sanitizedFollowDistance(distance);

    float yawRad = glm::radians(yaw);
    float pitchRad = glm::radians(pitch);

    glm::vec3 initialPos(0.0f, 1.5f, 3.0f);
    float initialYaw = 0.0f;
    float initialPitch = 0.0f;

    if (targetModel)
    {
        glm::vec3 targetCenter = followAnchorPosition(
            *targetModel,
            glm::vec3(targetOffset.x(), targetOffset.y(), targetOffset.z()));
        const FollowSettings initialFollowSettings = makeFollowSettings(
            yawRad,
            pitchRad,
            distance,
            smoothSpeed,
            glm::vec3(targetOffset.x(), targetOffset.y(), targetOffset.z()));

        const glm::vec3 modelForward = glm::vec3(targetModel->worldTransform[2]);
        const float targetYaw = FollowOrbit::computeTargetYaw(modelForward);
        const FollowOrbitPose initialPose = FollowOrbit::computePose(targetCenter, targetYaw, initialFollowSettings);
        initialPos = initialPose.position;
        initialYaw = initialPose.rotation.x;
        initialPitch = initialPose.rotation.y;
    }
    
    // Create the follow camera
    Camera* followCam = display->createCamera(
        cameraNameStd,
        initialPos,
        glm::vec2(initialYaw, initialPitch)
    );
    
    if (!followCam) {
        qDebug() << "[ViewportHost] Failed to create follow camera (internal)";
        return nullptr;
    }
    
    // Configure follow settings with all parameters
    const FollowSettings followSettings = makeFollowSettings(
        yawRad,
        pitchRad,
        distance,
        smoothSpeed,
        glm::vec3(targetOffset.x(), targetOffset.y(), targetOffset.z()));
    
    followCam->setFollowTarget(sceneIndex, followSettings);
    followCam->setFreeFlyCamera(false);
    // Follow target is already set via setFollowTarget above
    followCam->setCameraId(makeCameraId().toStdString());
    
    qDebug() << "[ViewportHost] Created follow camera (internal) for scene" << sceneIndex 
             << "distance" << distance << "yaw" << yaw << "pitch" << pitch 
             << "smoothSpeed" << smoothSpeed;
    
    return followCam;
}

void ViewportHostWidget::setCameraConfigs(const QList<CameraConfig>& configs)
{
    QList<CameraConfig> effectiveConfigs = configs;
    if (!hasFreeCameraConfig(effectiveConfigs))
    {
        CameraConfig freeConfig;
        freeConfig.id = makeCameraId();
        freeConfig.name = QStringLiteral("Main Fly Camera");
        freeConfig.type = CameraConfig::Type::Free;
        freeConfig.mode = QStringLiteral("FreeFly");
        freeConfig.freeFly = true;
        freeConfig.invertHorizontalDrag = true;
        freeConfig.position = QVector3D(0.0f, 0.0f, 3.0f);
        freeConfig.rotation = QVector3D(0.0f, 0.0f, 0.0f);
        freeConfig.nearClip = 0.1f;
        freeConfig.farClip = 100.0f;
        effectiveConfigs.push_front(freeConfig);
    }

    m_pendingCameraConfigs = effectiveConfigs;

    if (!m_runtime->display() || !m_runtime->engine()) {
        return;
    }
    
    // Clear existing cameras (except we'll recreate them)
    auto& cameras = m_runtime->display()->cameras;
    while (!cameras.empty()) {
        m_runtime->display()->removeCamera(cameras.back());
    }
    
    // Recreate cameras from configs
    for (const auto& config : effectiveConfigs) {
        if (config.type == CameraConfig::Type::Follow && config.followTargetIndex >= 0) {
            // Create follow camera with full settings
            Camera* followCam = createFollowCameraInternal(
                m_runtime->display(),
                m_runtime->engine(),
                config.followTargetIndex, 
                config.followDistance, 
                config.followYaw, 
                config.followPitch,
                config.followSmoothSpeed,
                config.followTargetOffset
            );
            if (followCam) {
                followCam->setCameraId((config.id.isEmpty() ? makeCameraId() : config.id).toStdString());
                if (!config.name.isEmpty())
                {
                    followCam->setCameraName(config.name.toStdString());
                }
                const CameraMode requestedMode = cameraModeFromString(
                    config.mode,
                    config.freeFly ? CameraMode::FreeFly : CameraMode::OrbitFollow);
                followCam->setMode(requestedMode);
                followCam->setInvertHorizontalDrag(config.invertHorizontalDrag);
                // Follow target is already set via createFollowCameraInternal
                followCam->setPerspectiveNearFar(config.nearClip, config.farClip);
            }
        } else {
            // Create free camera
            glm::vec3 pos(config.position.x(), config.position.y(), config.position.z());
            glm::vec2 rot(glm::radians(config.rotation.x()), glm::radians(config.rotation.y()));
            Camera* cam = m_runtime->display()->createCamera(config.name.toStdString(), pos, rot);
            if (cam) {
                cam->setCameraId((config.id.isEmpty() ? makeCameraId() : config.id).toStdString());
                const CameraMode requestedMode = cameraModeFromString(
                    config.mode,
                    config.freeFly ? CameraMode::FreeFly : CameraMode::Fixed);
                cam->setMode(requestedMode);
                cam->setInvertHorizontalDrag(config.invertHorizontalDrag);
                cam->setPerspectiveNearFar(config.nearClip, config.farClip);
            }
        }
    }
    
    // Ensure we have at least one camera
    if (m_runtime->display()->cameras.empty()) {
        Camera* camera = m_runtime->display()->createCamera("Main Fly Camera");
        if (camera) {
            camera->setCameraId(makeCameraId().toStdString());
            camera->setMode(CameraMode::FreeFly);
            camera->setInvertHorizontalDrag(true);
        }
    }

    updateViewportLayout();
    syncViewportSelectorChoices();
}

int ViewportHostWidget::ensureFollowCamera(int sceneIndex, float distance, float yaw, float pitch)
{
    if (!m_runtime->engine() || sceneIndex < 0 || 
        sceneIndex >= static_cast<int>(m_runtime->engine()->models.size()) ||
        !m_runtime->engine()->models[static_cast<size_t>(sceneIndex)]) {
        qDebug() << "[ViewportHost] Cannot create follow camera: invalid scene index" << sceneIndex;
        return -1;
    }
    
    if (!m_runtime->display()) {
        qDebug() << "[ViewportHost] Cannot create follow camera: display not initialized";
        return -1;
    }
    
    distance = sanitizedFollowDistance(distance);
    const float yawRad = glm::radians(yaw);
    const float pitchRad = glm::radians(pitch);
    const FollowSettings followSettings = makeFollowSettings(
        yawRad,
        pitchRad,
        distance,
        followcam::kDefaultSmoothSpeed);

    // Prefer follow-target state for identity, not camera naming.
    if (const int existingByState = findFollowCameraIndexForScene(m_runtime->display(), sceneIndex);
        existingByState >= 0)
    {
        qDebug() << "[ViewportHost] Reusing existing follow camera for scene" << sceneIndex;
        return existingByState;
    }

    // Build a stable display name for this follow camera.
    QString cameraName = QStringLiteral("Follow Cam (Scene %1)").arg(sceneIndex);
    std::string cameraNameStd = cameraName.toStdString();

    // Get target model
    Model* targetModel = m_runtime->engine()->models[static_cast<size_t>(sceneIndex)].get();
    if (!targetModel) {
        return -1;
    }
    
    glm::vec3 targetCenter = followAnchorPosition(*targetModel);

    const glm::vec3 modelForward = glm::vec3(targetModel->worldTransform[2]);
    const float targetYaw = FollowOrbit::computeTargetYaw(modelForward);
    const FollowOrbitPose initialPose = FollowOrbit::computePose(targetCenter, targetYaw, followSettings);
    
    // Create the follow camera
    Camera* followCam = m_runtime->display()->createCamera(
        cameraNameStd,
        initialPose.position,
        initialPose.rotation
    );
    
    if (!followCam) {
        qDebug() << "[ViewportHost] Failed to create follow camera";
        return -1;
    }
    if (followCam->getCameraId().empty()) {
        followCam->setCameraId(makeCameraId().toStdString());
    }
    
    followCam->setFollowTarget(sceneIndex, followSettings);
    followCam->setFreeFlyCamera(false);
    // Follow target is already set via setFollowTarget above
    
    qDebug() << "[ViewportHost] Created follow camera for scene" << sceneIndex 
             << "distance" << distance << "yaw" << yaw << "pitch" << pitch;

    updateViewportLayout();
    syncViewportSelectorChoices();
    return static_cast<int>(m_runtime->display()->cameras.size()) - 1;
}

int ViewportHostWidget::createFollowCamera(int sceneIndex, float distance, float yaw, float pitch)
{
    return ensureFollowCamera(sceneIndex, distance, yaw, pitch);
}

void ViewportHostWidget::ensureFollowCamerasForAllSceneItems()
{
    if (!m_runtime || !m_runtime->engine() || !m_runtime->display())
    {
        return;
    }

    const int modelCount = static_cast<int>(m_runtime->engine()->models.size());
    for (int sceneIndex = 0; sceneIndex < modelCount; ++sceneIndex)
    {
        if (!m_runtime->engine()->models[static_cast<size_t>(sceneIndex)])
        {
            continue;
        }
        ensureFollowCamera(sceneIndex, 5.0f, 0.0f, 20.0f);
    }
}

void ViewportHostWidget::deleteCamera(int cameraIndex)
{
    if (!m_runtime->display()) {
        return;
    }
    
    auto& cameras = m_runtime->display()->cameras;
    if (cameraIndex < 0 || cameraIndex >= static_cast<int>(cameras.size())) {
        return;
    }
    
    Camera* camera = cameras[cameraIndex];
    m_runtime->display()->removeCamera(camera);
    
    // Ensure we have at least one camera
    if (cameras.empty() && m_runtime->engine()) {
        Camera* newCamera = m_runtime->display()->createCamera("Main Fly Camera");
        if (newCamera) {
            newCamera->setCameraId(makeCameraId().toStdString());
            newCamera->setMode(CameraMode::FreeFly);
            newCamera->setInvertHorizontalDrag(true);
        }
    }

    updateViewportLayout();
    syncViewportSelectorChoices();
}

int ViewportHostWidget::activeCameraIndex() const
{
    if (!m_runtime->display())
    {
        return -1;
    }

    Camera* active = m_runtime->display()->getActiveCamera();
    if (!active)
    {
        return -1;
    }

    const auto& cameras = m_runtime->display()->cameras;
    auto it = std::find(cameras.begin(), cameras.end(), active);
    if (it == cameras.end())
    {
        return -1;
    }
    return static_cast<int>(std::distance(cameras.begin(), it));
}

int ViewportHostWidget::cameraIndexForId(const QString& cameraId) const
{
    if (!m_runtime->display() || cameraId.isEmpty())
    {
        return -1;
    }

    const auto& cameras = m_runtime->display()->cameras;
    for (size_t i = 0; i < cameras.size(); ++i)
    {
        if (cameras[i] && QString::fromStdString(cameras[i]->getCameraId()) == cameraId)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

QString ViewportHostWidget::activeCameraId() const
{
    if (!m_runtime->display())
    {
        return {};
    }

    Camera* camera = m_runtime->display()->getActiveCamera();
    return camera ? QString::fromStdString(camera->getCameraId()) : QString();
}

void ViewportHostWidget::setActiveCamera(int cameraIndex)
{
    if (!m_runtime->display()) {
        return;
    }
    
    auto& cameras = m_runtime->display()->cameras;
    if (cameraIndex < 0 || cameraIndex >= static_cast<int>(cameras.size())) {
        return;
    }
    
    Camera* previousActiveCamera = m_runtime->display()->getActiveCamera();

    // Get the camera that will become active (before rotation)
    Camera* newActiveCamera = cameras[cameraIndex];
    if (!newActiveCamera)
    {
        return;
    }
    if (newActiveCamera->getCameraId().empty())
    {
        newActiveCamera->setCameraId(makeCameraId().toStdString());
    }
    const QString newActiveCameraId = newActiveCamera
        ? QString::fromStdString(newActiveCamera->getCameraId())
        : QString();

    if (newActiveCamera && viewportCount() == 1)
    {
        ViewportLayout layout = viewportLayout();
        layout.cameraIds[0] = QString::fromStdString(newActiveCamera->getCameraId());
        m_viewportLayout = normalizedViewportLayout(layout);
    }
    
    m_runtime->display()->setActiveCamera(newActiveCamera);
    updateViewportLayout();

    // Keep green viewport focus in sync when camera is selected from hierarchy.
    // Only retarget when this camera is actually assigned to a visible viewport slot.
    if (!newActiveCameraId.isEmpty())
    {
        const ViewportLayout layout = normalizedViewportLayout(m_viewportLayout);
        for (int i = 0; i < layout.count; ++i)
        {
            const QString slotCameraId = layout.cameraIds.value(i);
            if (slotCameraId == newActiveCameraId)
            {
                m_focusedViewportIndex = i;
                break;
            }
            const int slotCameraIndex = cameraIndexForId(slotCameraId);
            if (slotCameraIndex == cameraIndex)
            {
                m_focusedViewportIndex = i;
                break;
            }
        }
    }
    updateViewportBorders();

    if (previousActiveCamera && previousActiveCamera != newActiveCamera)
    {
        // Preserve per-camera follow ownership when switching active camera.
        previousActiveCamera->clearInputState();
    }

    if (m_cameraChangedCallback)
    {
        m_cameraChangedCallback();
    }
}

void ViewportHostWidget::updateCameraConfig(int cameraIndex, const CameraConfig& config)
{
    if (!m_runtime->display()) {
        return;
    }
    
    auto& cameras = m_runtime->display()->cameras;
    if (cameraIndex < 0 || cameraIndex >= static_cast<int>(cameras.size())) {
        return;
    }
    
    Camera* camera = cameras[cameraIndex];
    if (!camera) return;
    
    if (!config.id.isEmpty()) {
        camera->setCameraId(config.id.toStdString());
    }
    camera->setCameraName(config.name.toStdString());
    
    if (config.type == CameraConfig::Type::Follow && config.followTargetIndex >= 0) {
        // Set up follow settings
        const FollowSettings fs = makeFollowSettings(
            glm::radians(config.followYaw),
            glm::radians(config.followPitch),
            config.followDistance,
            config.followSmoothSpeed,
            glm::vec3(config.followTargetOffset.x(), config.followTargetOffset.y(), config.followTargetOffset.z()));
        
        // Update follow target and settings
        camera->setFollowTarget(config.followTargetIndex, fs);
        const CameraMode requestedMode = cameraModeFromString(
            config.mode,
            config.freeFly ? CameraMode::FreeFly : CameraMode::OrbitFollow);
        camera->setMode(requestedMode);
        
        // Follow target is already set via setFollowTarget above
    } else if (config.type == CameraConfig::Type::Free) {
        // Update position for free camera
        camera->cameraPos = glm::vec3(config.position.x(), config.position.y(), config.position.z());
        camera->setEulerRotation(glm::vec2(glm::radians(config.rotation.x()), glm::radians(config.rotation.y())));
        
        // Disable follow mode if previously a follow camera
        if (camera->isFollowModeEnabled()) {
            camera->setFollowTarget(-1);
        }
        const CameraMode requestedMode = cameraModeFromString(
            config.mode,
            config.freeFly ? CameraMode::FreeFly : CameraMode::Fixed);
        camera->setMode(requestedMode);
    }
    camera->setInvertHorizontalDrag(config.invertHorizontalDrag);
    
    // Apply clipping planes
    camera->setPerspectiveNearFar(config.nearClip, config.farClip);
    
    updateViewportLayout();
    syncViewportSelectorChoices();
}

QString ViewportHostWidget::cameraIdForViewportIndex(int index) const
{
    const ViewportLayout layout = normalizedViewportLayout(m_viewportLayout);
    if (index < 0 || index >= layout.cameraIds.size())
    {
        return {};
    }
    return layout.cameraIds[index];
}

Camera* ViewportHostWidget::focusedViewportCamera() const
{
    Display* display = m_runtime ? m_runtime->display() : nullptr;
    if (!display)
    {
        return nullptr;
    }

    const QString cameraId = cameraIdForViewportIndex(m_focusedViewportIndex);
    if (cameraId.isEmpty())
    {
        return nullptr;
    }

    const int cameraIndex = cameraIndexForId(cameraId);
    if (cameraIndex < 0 || cameraIndex >= static_cast<int>(display->cameras.size()))
    {
        return nullptr;
    }

    return display->cameras[static_cast<size_t>(cameraIndex)];
}

QRect ViewportHostWidget::viewportRectForIndex(int index) const
{
    const int w = std::max(1, m_renderSurface ? m_renderSurface->width() : width());
    const int h = std::max(1, m_renderSurface ? m_renderSurface->height() : height());
    switch (viewportCount())
    {
        case 2:
            return (index == 0) ? QRect(0, 0, w / 2, h) : QRect(w / 2, 0, w - (w / 2), h);
        case 3:
            if (index == 0) return QRect(0, 0, w / 2, h);
            if (index == 1) return QRect(w / 2, 0, w - (w / 2), h / 2);
            return QRect(w / 2, h / 2, w - (w / 2), h - (h / 2));
        case 4:
            if (index == 0) return QRect(0, 0, w / 2, h / 2);
            if (index == 1) return QRect(w / 2, 0, w - (w / 2), h / 2);
            if (index == 2) return QRect(0, h / 2, w / 2, h - (h / 2));
            return QRect(w / 2, h / 2, w - (w / 2), h - (h / 2));
        case 1:
        default:
            return QRect(0, 0, w, h);
    }
}

void ViewportHostWidget::layoutViewportSelectors()
{
    while (m_viewportCameraSelectors.size() < viewportCount())
    {
        auto* combo = new QComboBox(m_viewportSelectorPanel ? m_viewportSelectorPanel : this);
        combo->setStyleSheet(QStringLiteral(
            "QComboBox { background: rgba(16,22,29,220); color: #edf2f7; border: 1px solid #2e3b4a; border-radius: 6px; padding: 3px 8px; }"
            "QComboBox QAbstractItemView { background: #1b2430; color: #edf2f7; border: 1px solid #2e3b4a; selection-background-color: #233142; }"));
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, combo](int idx)
        {
            if (idx < 0)
            {
                return;
            }
            const int viewportIndex = m_viewportCameraSelectors.indexOf(combo);
            if (viewportIndex < 0)
            {
                return;
            }
            ViewportLayout layout = viewportLayout();
            layout.cameraIds[viewportIndex] = combo->currentData().toString();
            setViewportLayout(layout);
            setFocusedViewportIndex(viewportIndex);
            if (layout.count == 1)
            {
                const int cameraIndex = cameraIndexForId(layout.cameraIds[viewportIndex]);
                if (cameraIndex >= 0)
                {
                    setActiveCamera(cameraIndex);
                }
            }
        });
        combo->show();
        combo->raise();
        m_viewportCameraSelectors.append(combo);
    }

    if (!m_viewportSelectorGrid)
    {
        return;
    }

    while (QLayoutItem* item = m_viewportSelectorGrid->takeAt(0))
    {
        delete item;
    }

    const int count = viewportCount();
    auto addSelector = [&](int selectorIndex, int row, int column, int rowSpan = 1, int columnSpan = 1)
    {
        if (selectorIndex < 0 || selectorIndex >= m_viewportCameraSelectors.size())
        {
            return;
        }
        QComboBox* combo = m_viewportCameraSelectors[selectorIndex];
        if (!combo)
        {
            return;
        }
        combo->setVisible(isVisible() && selectorIndex < count);
        if (combo->isVisible())
        {
            m_viewportSelectorGrid->addWidget(combo, row, column, rowSpan, columnSpan);
        }
    };

    for (QComboBox* combo : m_viewportCameraSelectors)
    {
        if (combo)
        {
            combo->setVisible(false);
        }
    }

    switch (count)
    {
        case 1:
            addSelector(0, 0, 0, 1, 2);
            break;
        case 2:
            addSelector(0, 0, 0);
            addSelector(1, 0, 1);
            break;
        case 3:
            addSelector(0, 0, 0);
            addSelector(1, 0, 1);
            addSelector(2, 1, 0, 1, 2);
            break;
        case 4:
        default:
            addSelector(0, 0, 0);
            addSelector(1, 0, 1);
            addSelector(2, 1, 0);
            addSelector(3, 1, 1);
            break;
    }

    for (int i = 0; i < m_viewportCameraSelectors.size(); ++i)
    {
        QComboBox* combo = m_viewportCameraSelectors[i];
        if (!combo)
        {
            continue;
        }
        combo->raise();
    }

    if (m_viewportSelectorPanel)
    {
        m_viewportSelectorPanel->setVisible(isVisible());
    }

    updateViewportBorders();
}

void ViewportHostWidget::syncViewportSelectorChoices()
{
    const auto configs = cameraConfigs();
    const ViewportLayout layout = viewportLayout();
    layoutViewportSelectors();

    for (int i = 0; i < m_viewportCameraSelectors.size(); ++i)
    {
        QComboBox* combo = m_viewportCameraSelectors[i];
        if (!combo || i >= layout.count)
        {
            continue;
        }

        combo->blockSignals(true);
        combo->clear();
        combo->addItem(QStringLiteral("<None>"), QString());
        for (const auto& config : configs)
        {
            combo->addItem(config.name.isEmpty() ? QStringLiteral("Camera") : config.name, config.id);
        }

        int selectedIndex = combo->findData(layout.cameraIds.value(i));
        if (selectedIndex < 0)
        {
            selectedIndex = 0;
        }
        if (selectedIndex >= 0)
        {
            combo->setCurrentIndex(selectedIndex);
        }
        combo->blockSignals(false);
    }
}

void ViewportHostWidget::updateViewportBorders()
{
    while (m_viewportBorders.size() < viewportCount())
    {
        auto* frame = new QFrame(m_renderSurface ? m_renderSurface : this);
        frame->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        frame->setStyleSheet(QStringLiteral("QFrame { background: transparent; border: 2px solid #6b7280; }"));
        frame->hide();
        m_viewportBorders.append(frame);
    }

    for (int i = 0; i < m_viewportBorders.size(); ++i)
    {
        QFrame* frame = m_viewportBorders[i];
        if (!frame)
        {
            continue;
        }
        frame->setParent(m_renderSurface ? m_renderSurface : this);
        const bool visible = isVisible() && i < viewportCount();
        frame->setVisible(visible);
        if (!visible)
        {
            continue;
        }

        const QRect rectLocal = viewportRectForIndex(i).adjusted(0, 0, -1, -1);
        frame->setGeometry(rectLocal);
        const QString borderColor = (i == m_focusedViewportIndex) ? QStringLiteral("#22c55e")
                                                                  : QStringLiteral("#6b7280");
        frame->setStyleSheet(QStringLiteral(
            "QFrame { background: transparent; border: 2px solid %1; }").arg(borderColor));
        frame->raise();
        frame->show();
    }

    const QString baseStyle = QStringLiteral(
        "QComboBox { background: rgba(16,22,29,220); color: #edf2f7; border: 1px solid #2e3b4a; border-radius: 6px; padding: 3px 8px; }"
        "QComboBox QAbstractItemView { background: #1b2430; color: #edf2f7; border: 1px solid #2e3b4a; selection-background-color: #233142; }");
    const QString focusedStyle = QStringLiteral(
        "QComboBox { background: rgba(16,22,29,235); color: #edf2f7; border: 2px solid #22c55e; border-radius: 6px; padding: 2px 7px; }"
        "QComboBox QAbstractItemView { background: #1b2430; color: #edf2f7; border: 1px solid #2e3b4a; selection-background-color: #233142; }");

    for (int i = 0; i < m_viewportCameraSelectors.size(); ++i)
    {
        QComboBox* combo = m_viewportCameraSelectors[i];
        if (!combo || !combo->isVisible())
        {
            continue;
        }
        combo->setStyleSheet(i == m_focusedViewportIndex ? focusedStyle : baseStyle);
    }
}

void ViewportHostWidget::moveEvent(QMoveEvent* event)
{
    QWidget::moveEvent(event);
    layoutViewportSelectors();
    updateViewportBorders();
}

int ViewportHostWidget::viewportIndexAt(const QPoint& position) const
{
    QPoint localPos = position;
    if (m_renderSurface)
    {
        localPos -= m_renderSurface->pos();
    }
    for (int i = 0; i < viewportCount(); ++i)
    {
        if (viewportRectForIndex(i).contains(localPos))
        {
            return i;
        }
    }
    return 0;
}

void ViewportHostWidget::setFocusedViewportIndex(int index)
{
    const int clampedIndex = std::clamp(index, 0, std::max(0, viewportCount() - 1));
    m_focusedViewportIndex = clampedIndex;
    updateViewportBorders();

    const QString focusedCameraId = cameraIdForViewportIndex(clampedIndex);
    const int cameraIndex = cameraIndexForId(focusedCameraId);
    if (cameraIndex >= 0)
    {
        setActiveCamera(cameraIndex);
        if (m_viewportFocusChangedCallback)
        {
            m_viewportFocusChangedCallback(focusedCameraId);
        }
    }
}

void ViewportHostWidget::updateViewportLayout()
{
    Display* display = m_runtime->display();
    if (!display)
    {
        return;
    }

    ViewportLayout layout = normalizedViewportLayout(m_viewportLayout);
    auto& cameras = display->cameras;
    QStringList availableIds;
    for (Camera* camera : cameras)
    {
        if (camera)
        {
            availableIds.append(QString::fromStdString(camera->getCameraId()));
        }
    }

    for (int i = 0; i < layout.count; ++i)
    {
        if (!layout.cameraIds[i].isEmpty() && !availableIds.contains(layout.cameraIds[i]))
        {
            layout.cameraIds[i].clear();
        }
    }
    m_viewportLayout = layout;

    for (Camera* camera : cameras)
    {
        if (!camera)
        {
            continue;
        }
        camera->setFullscreenViewportEnabled(false);
        camera->setViewport(0.0f, 0.0f, 1.0f, 1.0f);
    }

    std::vector<Display::ViewportSlot> viewportSlots;
    viewportSlots.reserve(static_cast<size_t>(layout.count));
    for (int i = 0; i < layout.count; ++i)
    {
        const QString cameraId = layout.cameraIds.value(i);
        if (cameraId.isEmpty())
        {
            continue;
        }

        const int cameraIndex = cameraIndexForId(cameraId);
        if (cameraIndex < 0 || cameraIndex >= static_cast<int>(cameras.size()))
        {
            continue;
        }

        Camera* camera = cameras[static_cast<size_t>(cameraIndex)];
        if (!camera)
        {
            continue;
        }

        const QRect rect = viewportRectForIndex(i);
        const float centerX = rect.x() + (rect.width() * 0.5f);
        const float centerY = rect.y() + (rect.height() * 0.5f);
        const float viewportWidth = static_cast<float>(std::max(1, rect.width()));
        const float viewportHeight = static_cast<float>(std::max(1, rect.height()));

        camera->setFullscreenViewportEnabled(false);
        camera->setViewport(centerX, centerY, viewportWidth, viewportHeight);

        Display::ViewportSlot slot;
        slot.camera = camera;
        slot.centerX = centerX;
        slot.centerY = centerY;
        slot.width = viewportWidth;
        slot.height = viewportHeight;
        viewportSlots.push_back(slot);
    }

    display->setViewportSlots(viewportSlots);

    if (layout.count == 1)
    {
        const int cameraIndex = cameraIndexForId(layout.cameraIds.value(0));
        if (cameraIndex >= 0 && cameraIndex < static_cast<int>(cameras.size()))
        {
            display->setActiveCamera(cameras[static_cast<size_t>(cameraIndex)]);
        }
    }
}

void ViewportHostWidget::setCameraPosition(const QVector3D& position)
{
    m_cameraController->setCameraPosition(position);
}

void ViewportHostWidget::setCameraRotation(const QVector3D& rotation)
{
    m_cameraController->setCameraRotation(rotation);
}

void ViewportHostWidget::setCameraSpeed(float speed)
{
    m_cameraController->setCameraSpeed(speed);
}

void ViewportHostWidget::setPerspectiveNearFar(float near, float far)
{
    m_cameraController->setPerspectiveNearFar(near, far);
}

void ViewportHostWidget::getPerspectiveNearFar(float& near, float& far) const
{
    m_cameraController->getPerspectiveNearFar(near, far);
}

void ViewportHostWidget::resetCamera()
{
    m_cameraController->resetCamera();
}

void ViewportHostWidget::setBackgroundColor(const QColor& color)
{
    m_runtime->setBackgroundColor(color.redF(), color.greenF(), color.blueF());
}

void ViewportHostWidget::setRenderPath(const QString& renderPath)
{
    const bool use2d = renderPath.compare(QStringLiteral("flat2d"), Qt::CaseInsensitive) == 0;
    if (m_runtime->use2DPipeline() == use2d)
    {
        return;
    }

    const QList<SceneItem> items = sceneItems();
    const QVector3D savedCameraPos = cameraPosition();
    const QVector3D savedCameraRot = cameraRotation();
    const float savedCameraSpeed = cameraSpeed();

    m_runtime->setUse2DPipeline(use2d);
    m_renderTimer.stop();
    m_runtime->shutdown();
    m_initialized = false;

    m_sceneController->pendingEntries().clear();
    for (const SceneItem& item : items)
    {
        m_sceneController->pendingEntries().push_back(item);
    }

    ensureViewportInitialized();
    m_cameraController->setCameraSpeed(savedCameraSpeed);
    m_cameraController->setCameraPosition(savedCameraPos);
    m_cameraController->setCameraRotation(savedCameraRot);
}

void ViewportHostWidget::setMeshConsolidationEnabled(bool enabled)
{
    m_sceneController->setMeshConsolidationEnabled(enabled);
    notifySceneChanged();
}

void ViewportHostWidget::createSceneLight()
{
    if (m_sceneLight.exists)
    {
        return;
    }

    m_sceneLight.exists = true;
    applySceneLightToRuntime();
    notifySceneChanged();
}

void ViewportHostWidget::setSceneLight(const SceneLight& light)
{
    m_sceneLight = light;
    applySceneLightToRuntime();
    notifySceneChanged();
}

void ViewportHostWidget::updateSceneItemTransform(int index, const QVector3D& translation, const QVector3D& rotation, const QVector3D& scale)
{
    m_sceneController->updateSceneItemTransform(index, translation, rotation, scale);
    notifySceneChanged();
}

bool ViewportHostWidget::alignSceneItemBottomToGround(int index, float groundY)
{
    auto& entries = m_sceneController->loadedEntries();
    if (!m_runtime || !m_runtime->engine() || index < 0 || index >= entries.size())
    {
        return false;
    }

    const QVector3D boundsSize = sceneItemBoundsSize(index);
    const QVector3D minPoint = sceneItemBoundsMin(index);
    if (boundsSize.lengthSquared() <= 1e-10f || !std::isfinite(minPoint.y()) || !std::isfinite(groundY))
    {
        return false;
    }

    const QVector3D translation = entries[index].translation;
    const QVector3D newTranslation(
        translation.x(),
        translation.y() + (groundY - minPoint.y()),
        translation.z());
    if (qFuzzyCompare(translation.y(), newTranslation.y()))
    {
        return true;
    }

    m_sceneController->updateSceneItemTransform(index, newTranslation, entries[index].rotation, entries[index].scale);
    notifySceneChanged();
    return true;
}

void ViewportHostWidget::setSceneItemMeshConsolidationEnabled(int index, bool enabled)
{
    m_sceneController->setSceneItemMeshConsolidationEnabled(index, enabled);
    notifySceneChanged();
}

void ViewportHostWidget::updateSceneItemPaintOverride(int index, bool enabled, const QVector3D& color)
{
    m_sceneController->updateSceneItemPaintOverride(index, enabled, color);
    notifySceneChanged();
}

void ViewportHostWidget::updateSceneItemAnimationState(int index, const QString& activeClip, bool playing, bool loop, float speed)
{
    m_sceneController->updateSceneItemAnimationState(index, activeClip, playing, loop, speed);
    notifySceneChanged();
}

void ViewportHostWidget::updateSceneItemAnimationProcessing(int index,
                                                            bool centroidNormalizationEnabled,
                                                            float trimStartNormalized,
                                                            float trimEndNormalized)
{
    m_sceneController->updateSceneItemAnimationProcessing(index,
                                                          centroidNormalizationEnabled,
                                                          trimStartNormalized,
                                                          trimEndNormalized);
    notifySceneChanged();
}

void ViewportHostWidget::updateSceneItemAnimationPhysicsCoupling(int index, const QString& couplingMode)
{
    if (!m_sceneController)
    {
        qWarning() << "[ViewportHost] Cannot update coupling - scene controller is null";
        return;
    }
    m_sceneController->updateSceneItemAnimationPhysicsCoupling(index, couplingMode);

    if (m_runtime && m_runtime->engine() && m_runtime->engine()->getPhysicsWorld() &&
        index >= 0 &&
        index < static_cast<int>(m_runtime->engine()->models.size()) &&
        m_runtime->engine()->models[static_cast<size_t>(index)])
    {
        reconfigurePhysicsBodyForMode(
            *m_runtime->engine()->models[static_cast<size_t>(index)],
            *m_runtime->engine()->getPhysicsWorld());
    }

    notifySceneChanged();
}

void ViewportHostWidget::updateSceneItemPhysicsGravity(int index, bool useGravity, const QVector3D& customGravity)
{
    m_sceneController->updateSceneItemPhysicsGravity(index, useGravity, customGravity);
    notifySceneChanged();
}

void ViewportHostWidget::updateSceneItemCharacterTurnResponsiveness(int index, float responsiveness)
{
    m_sceneController->updateSceneItemCharacterTurnResponsiveness(index, responsiveness);
    notifySceneChanged();
}

void ViewportHostWidget::updateSceneItemCharacterRestPointOnRelease(int index, bool enabled, float normalized)
{
    m_sceneController->updateSceneItemCharacterRestPointOnRelease(index, enabled, normalized);
    notifySceneChanged();
}

void ViewportHostWidget::updateSceneItemFocusSettings(int index, const QVector3D& focusPointOffset, float focusDistance)
{
    m_sceneController->updateSceneItemFocusSettings(index, focusPointOffset, focusDistance);
    m_sceneController->updateSceneItemFocusCameraOffset(index, QVector3D(0.0f, 0.0f, 0.0f), false);
    notifySceneChanged();
}

void ViewportHostWidget::updateSceneItemTextOverlay(int index, const SceneItem& textProps)
{
    m_sceneController->updateSceneItemTextOverlay(index, textProps);
    notifySceneChanged();
}

void ViewportHostWidget::captureSceneItemFocusFromCurrentCamera(int index)
{
    if (index < 0 || !m_runtime || !m_runtime->engine())
    {
        return;
    }

    Camera* camera = focusedViewportCamera();
    if (!camera)
    {
        camera = m_runtime->camera();
    }
    Model* model = modelForSceneIndex(m_runtime->engine(), index);
    if (!camera || !model)
    {
        return;
    }

    const glm::vec3 anchor = model->getFollowAnchorPosition();
    const glm::vec3 cameraPos = camera->cameraPos;
    glm::vec3 forward = camera->getForwardVector();
    if (glm::dot(forward, forward) <= 1e-8f)
    {
        const glm::vec3 towardAnchor = anchor - cameraPos;
        forward = glm::dot(towardAnchor, towardAnchor) > 1e-8f ? glm::normalize(towardAnchor) : glm::vec3(0.0f, 0.0f, 1.0f);
    }

    float projectionDistance = glm::dot(anchor - cameraPos, forward);
    if (!std::isfinite(projectionDistance) || projectionDistance < 0.05f)
    {
        projectionDistance = glm::length(anchor - cameraPos);
    }
    if (!std::isfinite(projectionDistance) || projectionDistance < 0.05f)
    {
        projectionDistance = 1.0f;
    }

    const glm::vec3 focusTarget = cameraPos + forward * projectionDistance;
    const glm::vec3 focusPointOffset = focusTarget - anchor;
    const glm::vec3 focusCameraOffset = cameraPos - focusTarget;
    const float focusDistance = glm::length(focusCameraOffset);

    m_sceneController->updateSceneItemFocusSettings(
        index,
        QVector3D(focusPointOffset.x, focusPointOffset.y, focusPointOffset.z),
        focusDistance);
    m_sceneController->updateSceneItemFocusCameraOffset(
        index,
        QVector3D(focusCameraOffset.x, focusCameraOffset.y, focusCameraOffset.z),
        true);
    notifySceneChanged();
}

void ViewportHostWidget::renameSceneItem(int index, const QString& name)
{
    m_sceneController->renameSceneItem(index, name);
    notifySceneChanged();
}

void ViewportHostWidget::setSceneItemVisible(int index, bool visible)
{
    m_sceneController->setSceneItemVisible(index, visible);
    notifySceneChanged();
}

void ViewportHostWidget::deleteSceneItem(int index)
{
    m_sceneController->deleteSceneItem(index);
    notifySceneChanged();
}

void ViewportHostWidget::setPrimitiveCullMode(int sceneIndex,
                                              int meshIndex,
                                              int primitiveIndex,
                                              const QString& cullMode,
                                              bool notify)
{
    if (!m_runtime->engine() || sceneIndex < 0 || meshIndex < 0 || primitiveIndex < 0)
    {
        return;
    }
    if (sceneIndex >= static_cast<int>(m_runtime->engine()->models.size()) || !m_runtime->engine()->models[static_cast<size_t>(sceneIndex)])
    {
        return;
    }
    auto& model = m_runtime->engine()->models[static_cast<size_t>(sceneIndex)];
    if (meshIndex >= static_cast<int>(model->meshes.size()))
    {
        return;
    }
    auto& mesh = model->meshes[static_cast<size_t>(meshIndex)];
    if (primitiveIndex >= static_cast<int>(mesh.primitives.size()) || !mesh.primitives[static_cast<size_t>(primitiveIndex)])
    {
        return;
    }

    PrimitiveCullMode mode = PrimitiveCullMode::Back;
    if (cullMode == QStringLiteral("none"))
    {
        mode = PrimitiveCullMode::Disabled;
    }
    else if (cullMode == QStringLiteral("front"))
    {
        mode = PrimitiveCullMode::Front;
    }
    mesh.primitives[static_cast<size_t>(primitiveIndex)]->cullMode = mode;
    if (mesh.primitives[static_cast<size_t>(primitiveIndex)]->ObjectTransformUBOMapped)
    {
        const ObjectTransform updated = mesh.primitives[static_cast<size_t>(primitiveIndex)]->buildObjectTransformData();
        memcpy(mesh.primitives[static_cast<size_t>(primitiveIndex)]->ObjectTransformUBOMapped, &updated, sizeof(updated));
    }

    auto& entries = m_sceneController->loadedEntries();
    if (sceneIndex >= 0 && sceneIndex < entries.size())
    {
        removePrimitiveOverrideIfDefault(entries[sceneIndex].primitiveOverrides,
                                         meshIndex,
                                         primitiveIndex,
                                         cullMode,
                                         mesh.primitives[static_cast<size_t>(primitiveIndex)]->forceAlphaOne);
        if (notify)
        {
            notifySceneChanged();
        }
    }
}

void ViewportHostWidget::setSceneItemCullMode(int sceneIndex, const QString& cullMode)
{
    if (cullMode == QStringLiteral("mixed") ||
        !m_runtime->engine() ||
        sceneIndex < 0 ||
        sceneIndex >= static_cast<int>(m_runtime->engine()->models.size()) ||
        !m_runtime->engine()->models[static_cast<size_t>(sceneIndex)])
    {
        return;
    }

    auto& model = m_runtime->engine()->models[static_cast<size_t>(sceneIndex)];
    for (size_t meshIndex = 0; meshIndex < model->meshes.size(); ++meshIndex)
    {
        auto& mesh = model->meshes[meshIndex];
        for (size_t primitiveIndex = 0; primitiveIndex < mesh.primitives.size(); ++primitiveIndex)
        {
            if (!mesh.primitives[primitiveIndex])
            {
                continue;
            }
            setPrimitiveCullMode(sceneIndex,
                                 static_cast<int>(meshIndex),
                                 static_cast<int>(primitiveIndex),
                                 cullMode,
                                 false);
        }
    }
    notifySceneChanged();
}

void ViewportHostWidget::setPrimitiveForceAlphaOne(int sceneIndex, int meshIndex, int primitiveIndex, bool enabled)
{
    if (!m_runtime->engine() || sceneIndex < 0 || meshIndex < 0 || primitiveIndex < 0)
    {
        return;
    }
    if (sceneIndex >= static_cast<int>(m_runtime->engine()->models.size()) || !m_runtime->engine()->models[static_cast<size_t>(sceneIndex)])
    {
        return;
    }
    auto& model = m_runtime->engine()->models[static_cast<size_t>(sceneIndex)];
    if (meshIndex >= static_cast<int>(model->meshes.size()))
    {
        return;
    }
    auto& mesh = model->meshes[static_cast<size_t>(meshIndex)];
    if (primitiveIndex >= static_cast<int>(mesh.primitives.size()) || !mesh.primitives[static_cast<size_t>(primitiveIndex)])
    {
        return;
    }

    mesh.primitives[static_cast<size_t>(primitiveIndex)]->forceAlphaOne = enabled;
    if (mesh.primitives[static_cast<size_t>(primitiveIndex)]->ObjectTransformUBOMapped)
    {
        const ObjectTransform updated = mesh.primitives[static_cast<size_t>(primitiveIndex)]->buildObjectTransformData();
        memcpy(mesh.primitives[static_cast<size_t>(primitiveIndex)]->ObjectTransformUBOMapped, &updated, sizeof(updated));
    }

    auto& entries = m_sceneController->loadedEntries();
    if (sceneIndex >= 0 && sceneIndex < entries.size())
    {
        removePrimitiveOverrideIfDefault(entries[sceneIndex].primitiveOverrides,
                                         meshIndex,
                                         primitiveIndex,
                                         primitiveCullMode(sceneIndex, meshIndex, primitiveIndex),
                                         enabled);
        notifySceneChanged();
    }
}

void ViewportHostWidget::setCoordinatePlaneIndicatorsEnabled(bool enabled)
{
    m_sceneController->setCoordinatePlaneIndicatorsEnabled(enabled);
    notifySceneChanged();
}

bool ViewportHostWidget::coordinatePlaneIndicatorsEnabled() const
{
    return m_sceneController->coordinatePlaneIndicatorsEnabled();
}

void ViewportHostWidget::relocateSceneItemInFrontOfCamera(int index)
{
    Camera* selectedCamera = focusedViewportCamera();
    if (!selectedCamera)
    {
        selectedCamera = m_runtime->camera();
    }
    m_cameraController->relocateSceneItemInFrontOfCamera(index, selectedCamera);
    notifySceneChanged();
}

void ViewportHostWidget::focusSceneItem(int index)
{
    Camera* selectedCamera = focusedViewportCamera();
    if (!selectedCamera)
    {
        selectedCamera = m_runtime->camera();
    }
    m_cameraController->focusSceneItem(index, selectedCamera);
    if (m_cameraChangedCallback)
    {
        m_cameraChangedCallback();
    }
}

void ViewportHostWidget::setSceneChangedCallback(std::function<void(const QList<SceneItem>&)> callback)
{
    m_sceneChangedCallback = std::move(callback);
}

void ViewportHostWidget::setCameraChangedCallback(std::function<void()> callback)
{
    m_cameraChangedCallback = std::move(callback);
}

void ViewportHostWidget::setViewportFocusChangedCallback(std::function<void(const QString& cameraId)> callback)
{
    m_viewportFocusChangedCallback = std::move(callback);
}

void ViewportHostWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (!m_initialized && !m_initScheduled)
    {
        m_initScheduled = true;
        QTimer::singleShot(0, this, [this]()
        {
            m_initScheduled = false;
            ensureViewportInitialized();
        });
    }
}

void ViewportHostWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_runtime)
    {
        const int w = m_renderSurface ? m_renderSurface->width() : width();
        const int h = m_renderSurface ? m_renderSurface->height() : height();
        m_runtime->resize(w, h);
    }
    updateViewportLayout();
    layoutViewportSelectors();
    updateViewportBorders();
    updateCameraDirectionIndicator();
}

void ViewportHostWidget::focusInEvent(QFocusEvent* event)
{
    QWidget::focusInEvent(event);
    if (m_runtime)
    {
        m_runtime->focusNativeWindow(static_cast<unsigned long>(winId()));
    }
    updateViewportBorders();
}

void ViewportHostWidget::focusOutEvent(QFocusEvent* event)
{
    QWidget::focusOutEvent(event);
    if (m_runtime)
    {
        m_runtime->clearInputState();
    }
    if (m_cameraChangedCallback)
    {
        m_cameraChangedCallback();
    }
    updateViewportBorders();
}

void ViewportHostWidget::mousePressEvent(QMouseEvent* event)
{
    setFocus(Qt::MouseFocusReason);
    if (m_renderSurface)
    {
        m_renderSurface->setFocus(Qt::MouseFocusReason);
    }
    setFocusedViewportIndex(viewportIndexAt(event->pos()));
    
    // In follow mode, camera handles orbit input directly.
    if (event->button() == Qt::RightButton && !isFreeFlyCameraEnabled())
    {
        setCursor(Qt::ClosedHandCursor);
    }

    if (event->button() == Qt::RightButton)
    {
        Camera* targetCamera = focusedViewportCamera();
        if (targetCamera)
        {
            targetCamera->setExternalMouseInput(true);
            const QPoint renderPos = m_renderSurface ? m_renderSurface->mapFrom(this, event->pos()) : event->pos();
            targetCamera->handleMouseButton(GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS, 0);
            targetCamera->handleCursorPos(static_cast<double>(renderPos.x()), static_cast<double>(renderPos.y()));
        }
    }
    
    QWidget::mousePressEvent(event);
}

void ViewportHostWidget::mouseMoveEvent(QMouseEvent* event)
{
    Camera* targetCamera = focusedViewportCamera();
    if (targetCamera)
    {
        targetCamera->setExternalMouseInput(true);
        const QPoint renderPos = m_renderSurface ? m_renderSurface->mapFrom(this, event->pos()) : event->pos();
        targetCamera->handleCursorPos(static_cast<double>(renderPos.x()), static_cast<double>(renderPos.y()));
    }
    
    QWidget::mouseMoveEvent(event);
}

void ViewportHostWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::RightButton)
    {
        unsetCursor();
    }

    if (event->button() == Qt::RightButton)
    {
        Camera* targetCamera = focusedViewportCamera();
        if (targetCamera)
        {
            targetCamera->setExternalMouseInput(true);
            targetCamera->handleMouseButton(GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE, 0);
        }
    }
    
    QWidget::mouseReleaseEvent(event);
}

void ViewportHostWidget::wheelEvent(QWheelEvent* event)
{
    if (!isFreeFlyCameraEnabled())
    {
        // Zoom in/out by changing follow distance on active follow camera.
        const float zoomSpeed = 0.001f;
        m_orbitDistance -= event->angleDelta().y() * zoomSpeed;
        m_orbitDistance = glm::clamp(m_orbitDistance, kMinOrbitDistance, kMaxOrbitDistance);

        Camera* targetCamera = focusedViewportCamera();
        if (targetCamera && targetCamera->isFollowModeEnabled())
        {
            FollowSettings settings = targetCamera->getFollowSettings();
            settings.distance = glm::clamp(settings.distance - event->angleDelta().y() * zoomSpeed,
                                           followcam::kMinDistance,
                                           100.0f);
            targetCamera->setFollowSettings(settings);
        }
    }
    
    QWidget::wheelEvent(event);
}

void ViewportHostWidget::keyPressEvent(QKeyEvent* event)
{
    qDebug() << "[ViewportHost] keyPressEvent:" << event->key();
    
    Camera* targetCamera = focusedViewportCamera();
    if (!targetCamera)
    {
        QWidget::keyPressEvent(event);
        return;
    }
    
    int glfwKey = -1;
    switch (event->key())
    {
        case Qt::Key_W: glfwKey = GLFW_KEY_W; break;
        case Qt::Key_A: glfwKey = GLFW_KEY_A; break;
        case Qt::Key_S: glfwKey = GLFW_KEY_S; break;
        case Qt::Key_D: glfwKey = GLFW_KEY_D; break;
        case Qt::Key_Q: glfwKey = GLFW_KEY_Q; break;
        case Qt::Key_E: glfwKey = GLFW_KEY_E; break;
        case Qt::Key_R: glfwKey = GLFW_KEY_R; break;
        case Qt::Key_O: glfwKey = GLFW_KEY_O; break;
        case Qt::Key_P: glfwKey = GLFW_KEY_P; break;
    }
    
    if (glfwKey >= 0)
    {
        if (Display* display = m_runtime ? m_runtime->display() : nullptr)
        {
            display->handleKey(glfwKey, 0, GLFW_PRESS, 0);
        }
        else
        {
            targetCamera->handleKey(glfwKey, 0, GLFW_PRESS, 0);
        }
    }
    else
    {
        QWidget::keyPressEvent(event);
    }
}

void ViewportHostWidget::keyReleaseEvent(QKeyEvent* event)
{
    Camera* targetCamera = focusedViewportCamera();
    if (!targetCamera)
    {
        QWidget::keyReleaseEvent(event);
        return;
    }
    
    // Map Qt key to GLFW key and forward to camera
    int glfwKey = -1;
    switch (event->key())
    {
        case Qt::Key_W: glfwKey = GLFW_KEY_W; break;
        case Qt::Key_A: glfwKey = GLFW_KEY_A; break;
        case Qt::Key_S: glfwKey = GLFW_KEY_S; break;
        case Qt::Key_D: glfwKey = GLFW_KEY_D; break;
        case Qt::Key_Q: glfwKey = GLFW_KEY_Q; break;
        case Qt::Key_E: glfwKey = GLFW_KEY_E; break;
        case Qt::Key_R: glfwKey = GLFW_KEY_R; break;
        case Qt::Key_O: glfwKey = GLFW_KEY_O; break;
        case Qt::Key_P: glfwKey = GLFW_KEY_P; break;
    }
    
    if (glfwKey >= 0)
    {
        if (Display* display = m_runtime ? m_runtime->display() : nullptr)
        {
            display->handleKey(glfwKey, 0, GLFW_RELEASE, 0);
        }
        else
        {
            targetCamera->handleKey(glfwKey, 0, GLFW_RELEASE, 0);
        }
    }
    else
    {
        QWidget::keyReleaseEvent(event);
    }
}

void ViewportHostWidget::dragEnterEvent(QDragEnterEvent* event)
{
    if (!event || !event->mimeData() || !event->mimeData()->hasUrls())
    {
        qDebug() << "[ViewportHost] dragEnterEvent ignored: no URLs";
        return;
    }

    for (const QUrl& url : event->mimeData()->urls())
    {
        if (url.isLocalFile() && detail::isRenderableAsset(url.toLocalFile()))
        {
            qDebug() << "[ViewportHost] dragEnterEvent accepted:" << url.toLocalFile();
            event->acceptProposedAction();
            return;
        }
    }

    qDebug() << "[ViewportHost] dragEnterEvent rejected URLs:" << event->mimeData()->urls();
}

void ViewportHostWidget::dropEvent(QDropEvent* event)
{
    if (!event || !event->mimeData() || !event->mimeData()->hasUrls())
    {
        qDebug() << "[ViewportHost] dropEvent ignored: no URLs";
        return;
    }

    bool accepted = false;
    for (const QUrl& url : event->mimeData()->urls())
    {
        const QString path = url.toLocalFile();
        if (path.isEmpty() || !detail::isRenderableAsset(path))
        {
            qDebug() << "[ViewportHost] dropEvent skipping non-renderable path:" << path;
            continue;
        }
        qDebug() << "[ViewportHost] dropEvent adding asset:" << path;
        addAssetToScene(path);
        accepted = true;
    }

    if (accepted)
    {
        event->acceptProposedAction();
    }
    else
    {
        qDebug() << "[ViewportHost] dropEvent accepted nothing";
    }
}

void ViewportHostWidget::ensureViewportInitialized()
{
    if (m_initialized)
    {
        return;
    }

    try
    {
        m_runtime->initialize(width(), height(), m_runtime->use2DPipeline());
        if (Display* display = m_runtime->display())
        {
            display->setMouseButtonEventCallback([this](int button, int action, int, double xpos, double ypos)
            {
                if (button != GLFW_MOUSE_BUTTON_LEFT || action != GLFW_PRESS)
                {
                    return;
                }

                const QPoint renderPoint(static_cast<int>(std::lround(xpos)),
                                         static_cast<int>(std::lround(ypos)));
                const QPoint hostPoint = m_renderSurface
                                             ? m_renderSurface->mapTo(this, renderPoint)
                                             : renderPoint;
                QMetaObject::invokeMethod(this, [this, hostPoint]()
                {
                    setFocus(Qt::MouseFocusReason);
                    if (m_renderSurface)
                    {
                        m_renderSurface->setFocus(Qt::MouseFocusReason);
                    }
                    setFocusedViewportIndex(viewportIndexAt(hostPoint));
                }, Qt::QueuedConnection);
            });
        }
        m_cameraController->setCameraSpeed(m_cameraController->cameraSpeed());
        applySceneLightToRuntime();
        m_initialized = true;

        if (!m_sceneController->pendingEntries().isEmpty())
        {
            m_sceneController->restorePendingEntries();
            if (!m_pendingCameraConfigs.isEmpty())
            {
                setCameraConfigs(m_pendingCameraConfigs);
            }
            notifySceneChanged();
        }
        else
        {
            const std::filesystem::path scenePath = detail::defaultScenePath();
            if (!scenePath.empty())
            {
                const QString path = QString::fromStdString(scenePath.string());
                m_sceneController->addAssetToScene(path);
            }
            else if (!m_sceneController->currentAssetPath().isEmpty())
            {
                m_sceneController->addAssetToScene(m_sceneController->currentAssetPath());
            }
        }

        const int renderW = m_renderSurface ? m_renderSurface->width() : width();
        const int renderH = m_renderSurface ? m_renderSurface->height() : height();
        const unsigned long targetWinId = static_cast<unsigned long>(m_renderSurface ? m_renderSurface->winId() : winId());
        m_runtime->embedNativeWindow(targetWinId);
        m_runtime->resize(renderW, renderH);
        updateViewportLayout();
        syncViewportSelectorChoices();
        layoutViewportSelectors();
        if (m_statusLabel)
        {
            m_statusLabel->hide();
        }
        m_renderTimer.start();
    }
    catch (const std::exception& ex)
    {
        if (m_statusLabel)
        {
            const QString baseMessage = QStringLiteral("Viewport unavailable:\n%1").arg(QString::fromUtf8(ex.what()));
            const QString helpText = QStringLiteral(
                "\n\nVulkan hardware device not found.\n"
                "Install drivers and retry:\n"
                "  - Intel/AMD: sudo apt-get install mesa-vulkan-drivers\n"
                "  - NVIDIA: install nvidia-driver-XXX\n"
                "Verify with: vulkaninfo");
            m_statusLabel->setText(baseMessage + helpText);
            m_statusLabel->show();
        }
        m_runtime->shutdown();
    }
}

void ViewportHostWidget::addAssetToScene(const QString& path)
{
    m_sceneController->addAssetToScene(path);
    m_tpsBootstrapPending = true;
    m_tpsBootstrapApplied = false;
    bootstrapThirdPersonShooter(false);
    notifySceneChanged();
}

void ViewportHostWidget::addTextOverlayToScene()
{
    m_sceneController->addTextOverlayItem();
    notifySceneChanged();
}

void ViewportHostWidget::renderFrame()
{
    if (!m_initialized || !m_runtime || !m_runtime->display() || !m_runtime->display()->window)
    {
        return;
    }
    if (glfwWindowShouldClose(m_runtime->display()->window))
    {
        m_renderTimer.stop();
        return;
    }

    static auto lastFrameTime = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    const double deltaSeconds = std::chrono::duration<double>(now - lastFrameTime).count();
    lastFrameTime = now;
    
    const float dt = static_cast<float>(deltaSeconds);

    if (m_runtime->engine())
    {
        if (m_tpsBootstrapPending)
        {
            bootstrapThirdPersonShooter(false);
        }

        auto* physicsWorld = m_runtime->engine()->getPhysicsWorld();
        auto& entries = m_sceneController->loadedEntries();
        Model* environmentSurface = nullptr;
        for (size_t i = 0; i < m_runtime->engine()->models.size() && i < static_cast<size_t>(entries.size()); ++i)
        {
            if (pathLooksLikeEnvironmentScene(entries[static_cast<int>(i)].sourcePath) &&
                m_runtime->engine()->models[i])
            {
                environmentSurface = m_runtime->engine()->models[i].get();
                break;
            }
        }
        for (size_t i = 0; i < m_runtime->engine()->models.size() && i < static_cast<size_t>(entries.size()); ++i)
        {
            const auto& entry = entries[static_cast<int>(i)];
            if (m_runtime->engine()->models[i])
            {
                auto& model = m_runtime->engine()->models[i];

                if (physicsWorld && couplingRequiresPhysics(*model) && !model->getPhysicsBody())
                {
                    reconfigurePhysicsBodyForMode(*model, *physicsWorld);
                }

                // Update character/controller state before stepping the world.
                if (model->character.isControllable)
                {
                    if (environmentSurface && environmentSurface != model.get())
                    {
                        updateCharacterGroundFromSceneSurface(*model, *environmentSurface);
                    }

                    if (physicsWorld && couplingRequiresPhysics(*model))
                    {
                        model->updateCharacterPhysics(dt, *physicsWorld);
                    }
                    else
                    {
                        model->updateCharacterPhysics(dt);
                    }

                    if (environmentSurface && environmentSurface != model.get())
                    {
                        resolveCharacterWallsFromScene(*model, *environmentSurface);
                    }
                }
                else if (physicsWorld && model->getPhysicsBody() && couplingUsesKinematicBody(*model))
                {
                    model->getPhysicsBody()->syncTransformToPhysics();
                }

                // Single-owner rule: controllable characters drive their own animation
                // state from character input/physics. Scene-entry playback controls
                // apply only to non-controllable scene items.
                if (!model->character.isControllable)
                {
                    model->setAnimationPlaybackState(entry.activeAnimationClip.toStdString(),
                                                     entry.animationPlaying,
                                                     entry.animationLoop,
                                                     entry.animationSpeed);
                }
                model->updateAnimation(deltaSeconds);
            }
        }

        if (physicsWorld)
        {
            m_runtime->engine()->updatePhysics(dt);
        }
        
        if (m_runtime->display())
        {
            // Shared camera/input runtime update path used by both editor and standalone.
            m_runtime->display()->updateRuntimeControllers(dt);
        }
    }

    captureMotionDebugFrame(dt);
    if (m_motionDebugOverlayOptions.enabled)
    {
        updateMotionDebugOverlay();
    }
    else if (m_runtime && m_runtime->display())
    {
        m_runtime->display()->clearCustomOverlayBitmap();
    }
    updateCameraDirectionIndicator();
    m_runtime->render();
    notifyCameraChangedIfNeeded();
}

// Runtime camera/input updates are delegated to Display::updateRuntimeControllers().

void ViewportHostWidget::notifyCameraChangedIfNeeded()
{
    if (!m_runtime->camera() || !m_cameraChangedCallback)
    {
        return;
    }

    const QVector3D position(m_runtime->camera()->cameraPos.x, m_runtime->camera()->cameraPos.y, m_runtime->camera()->cameraPos.z);
    const glm::vec2 rotationEuler = m_runtime->camera()->getEulerRotation();
    const QVector3D rotation(rotationEuler.y, rotationEuler.x, 0.0f);
    if (m_hasEmittedCameraState &&
        detail::vectorsNearlyEqual(position, m_lastEmittedCameraPosition) &&
        detail::vectorsNearlyEqual(rotation, m_lastEmittedCameraRotation))
    {
        return;
    }

    m_lastEmittedCameraPosition = position;
    m_lastEmittedCameraRotation = rotation;
    m_hasEmittedCameraState = true;
    m_cameraChangedCallback();
}

void ViewportHostWidget::notifySceneChanged()
{
    if (!m_sceneChangedCallback)
    {
        return;
    }
    
    // Defer to main thread if called from worker thread
    // This prevents crashes when parallel loader invokes callbacks
    QTimer::singleShot(0, this, [this]() {
        if (m_sceneChangedCallback)
        {
            m_sceneChangedCallback(sceneItems());
        }
    });
}

void ViewportHostWidget::applySceneLightToRuntime()
{
    if (!m_runtime->engine())
    {
        return;
    }

    if (m_sceneLight.exists)
    {
        const Light engineLight = detail::engineLightFromSceneLight(m_sceneLight);
        m_sceneLight.ambient = QVector3D(engineLight.ambient.x, engineLight.ambient.y, engineLight.ambient.z);
        m_sceneLight.diffuse = QVector3D(engineLight.diffuse.x, engineLight.diffuse.y, engineLight.diffuse.z);
        m_runtime->engine()->setLight(engineLight);
    }
    else
    {
        m_runtime->engine()->setLight(Light(glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f), glm::vec3(0.0f)));
    }
}

}  // namespace motive::ui
