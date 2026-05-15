#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif

#include "animation.h"

#include "model.h"
#include "ufbx.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <optional>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

namespace {

glm::vec3 toGlmVec3(ufbx_vec3 value)
{
    return glm::vec3(static_cast<float>(value.x),
                     static_cast<float>(value.y),
                     static_cast<float>(value.z));
}

glm::vec2 toGlmVec2(ufbx_vec2 value)
{
    return glm::vec2(static_cast<float>(value.x),
                     static_cast<float>(value.y));
}

glm::mat4 toGlmMat4(const ufbx_matrix& value)
{
    glm::mat4 result(1.0f);
    result[0][0] = static_cast<float>(value.m00); result[0][1] = static_cast<float>(value.m10); result[0][2] = static_cast<float>(value.m20); result[0][3] = 0.0f;
    result[1][0] = static_cast<float>(value.m01); result[1][1] = static_cast<float>(value.m11); result[1][2] = static_cast<float>(value.m21); result[1][3] = 0.0f;
    result[2][0] = static_cast<float>(value.m02); result[2][1] = static_cast<float>(value.m12); result[2][2] = static_cast<float>(value.m22); result[2][3] = 0.0f;
    result[3][0] = static_cast<float>(value.m03); result[3][1] = static_cast<float>(value.m13); result[3][2] = static_cast<float>(value.m23); result[3][3] = 1.0f;
    return result;
}

glm::vec3 readVec3KeyValue(const QJsonArray& array, int startIndex)
{
    return glm::vec3(static_cast<float>(array.at(startIndex).toDouble()),
                     static_cast<float>(array.at(startIndex + 1).toDouble()),
                     static_cast<float>(array.at(startIndex + 2).toDouble()));
}

glm::quat quatFromEulerDegrees(const glm::vec3& degrees)
{
    const glm::vec3 radians = glm::radians(degrees);
    return glm::normalize(glm::quat(radians));
}

const ufbx_mesh* findMeshByElementId(const ufbx_scene* scene, uint32_t elementId)
{
    if (!scene)
    {
        return nullptr;
    }
    for (size_t i = 0; i < scene->meshes.count; ++i)
    {
        const ufbx_mesh* mesh = scene->meshes.data[i];
        if (mesh && mesh->element_id == elementId)
        {
            return mesh;
        }
    }
    return nullptr;
}

std::vector<Vertex> buildAnimatedVerticesFromFbxMesh(const ufbx_mesh* mesh, const std::vector<uint32_t>& vertexIndices)
{
    std::vector<Vertex> vertices;
    if (!mesh || vertexIndices.empty())
    {
        return vertices;
    }

    vertices.resize(vertexIndices.size());
    for (size_t i = 0; i < vertexIndices.size(); ++i)
    {
        const size_t index = vertexIndices[i];
        Vertex vertex{};
        const bool useSkinnedPosition = mesh->skinned_position.exists;
        const bool useSkinnedNormal = mesh->skinned_normal.exists;
        vertex.pos = useSkinnedPosition
            ? toGlmVec3(ufbx_get_vertex_vec3(&mesh->skinned_position, index))
            : toGlmVec3(ufbx_get_vertex_vec3(&mesh->vertex_position, index));
        vertex.normal = useSkinnedNormal
            ? toGlmVec3(ufbx_get_vertex_vec3(&mesh->skinned_normal, index))
            : (mesh->vertex_normal.exists
                ? toGlmVec3(ufbx_get_vertex_vec3(&mesh->vertex_normal, index))
                : glm::vec3(0.0f, 1.0f, 0.0f));
        if (mesh->vertex_uv.exists)
        {
            glm::vec2 uv = toGlmVec2(ufbx_get_vertex_vec2(&mesh->vertex_uv, index));
            // Keep FBX runtime path consistent with import path: flip V.
            uv.y = 1.0f - uv.y;
            vertex.texCoord = uv;
        }
        else
        {
            vertex.texCoord = glm::vec2(0.0f);
        }
        vertices[i] = vertex;
    }
    return vertices;
}

glm::vec3 normalizeControllableCharacterRootOffset(Model& model, std::vector<Vertex>& vertices)
{
    if (!model.character.isControllable || vertices.empty() || !model.followAnchorLocalCenterInitialized)
    {
        return glm::vec3(0.0f);
    }

    glm::vec3 minPos(std::numeric_limits<float>::max());
    glm::vec3 maxPos(std::numeric_limits<float>::lowest());
    for (const Vertex& v : vertices)
    {
        minPos = glm::min(minPos, v.pos);
        maxPos = glm::max(maxPos, v.pos);
    }
    const glm::vec3 animatedCenter = (minPos + maxPos) * 0.5f;
    const glm::vec3 centerDelta = animatedCenter - model.followAnchorLocalCenter;
    if (glm::length(centerDelta) <= 1e-6f)
    {
        return glm::vec3(0.0f);
    }

    for (Vertex& v : vertices)
    {
        v.pos -= centerDelta;
    }
    return centerDelta;
}

double initialPlaybackTime(const motive::animation::FbxClipRuntime& clip, bool playing)
{
    if (!playing)
    {
        return clip.timeBegin;
    }

    const double clipDuration = std::max(clip.timeEnd - clip.timeBegin, 0.0);
    if (clipDuration <= 0.0)
    {
        return clip.timeBegin;
    }

    // Many authored FBX clips include several bind/rest samples at clip start.
    // Prime playback forward so the first visible frame is an animated pose.
    const double frameStep = 1.0 / 30.0;
    const double primeOffset = std::min(0.10, clipDuration);
    return std::min(clip.timeBegin + std::max(frameStep, primeOffset), clip.timeEnd);
}

struct ClipPlaybackWindow
{
    double start = 0.0;
    double end = 0.0;
    double duration = 0.0;
};

ClipPlaybackWindow clipPlaybackWindow(const motive::animation::FbxClipRuntime& clip,
                                      const motive::animation::FbxRuntime& runtime)
{
    ClipPlaybackWindow window;
    const double clipDuration = std::max(clip.timeEnd - clip.timeBegin, 0.0);
    if (clipDuration <= 0.0)
    {
        window.start = clip.timeBegin;
        window.end = clip.timeBegin;
        return window;
    }

    float trimStart = std::clamp(runtime.trimStartNormalized, 0.0f, 1.0f);
    float trimEnd = std::clamp(runtime.trimEndNormalized, 0.0f, 1.0f);
    if (trimEnd < trimStart)
    {
        std::swap(trimStart, trimEnd);
    }

    window.start = clip.timeBegin + static_cast<double>(trimStart) * clipDuration;
    window.end = clip.timeBegin + static_cast<double>(trimEnd) * clipDuration;
    if ((window.end - window.start) <= 1e-6)
    {
        window.start = clip.timeBegin;
        window.end = clip.timeEnd;
    }
    window.duration = std::max(window.end - window.start, 0.0);
    return window;
}

double clampToPlaybackWindow(double timeSeconds, const ClipPlaybackWindow& window)
{
    if (window.duration <= 0.0)
    {
        return window.start;
    }
    return std::clamp(timeSeconds, window.start, window.end);
}

template <typename Keyframe, typename Value, typename LerpFn>
Value sampleKeyframes(const std::vector<Keyframe>& keys,
                      double timeSeconds,
                      const Value& fallback,
                      LerpFn&& lerp)
{
    if (keys.empty())
    {
        return fallback;
    }
    if (keys.size() == 1 || timeSeconds <= keys.front().timeSeconds)
    {
        return keys.front().value;
    }
    if (timeSeconds >= keys.back().timeSeconds)
    {
        return keys.back().value;
    }

    for (size_t i = 1; i < keys.size(); ++i)
    {
        if (timeSeconds <= keys[i].timeSeconds)
        {
            const Keyframe& a = keys[i - 1];
            const Keyframe& b = keys[i];
            const double span = std::max(b.timeSeconds - a.timeSeconds, 1e-6);
            const float t = static_cast<float>((timeSeconds - a.timeSeconds) / span);
            return lerp(a.value, b.value, t);
        }
    }

    return keys.back().value;
}

bool sortVec3KeysByTime(const motive::animation::FbxVec3Keyframe& a,
                        const motive::animation::FbxVec3Keyframe& b)
{
    return a.timeSeconds < b.timeSeconds;
}

bool sortQuatKeysByTime(const motive::animation::FbxQuatKeyframe& a,
                        const motive::animation::FbxQuatKeyframe& b)
{
    return a.timeSeconds < b.timeSeconds;
}

void sortClipKeys(motive::animation::FbxCustomClip& clip)
{
    for (motive::animation::FbxCustomTrack& track : clip.tracks)
    {
        std::sort(track.translationKeys.begin(), track.translationKeys.end(), sortVec3KeysByTime);
        std::sort(track.rotationKeys.begin(), track.rotationKeys.end(), sortQuatKeysByTime);
        std::sort(track.scaleKeys.begin(), track.scaleKeys.end(), sortVec3KeysByTime);
    }
}

bool parseVec3KeyArray(const QJsonArray& jsonKeys, std::vector<motive::animation::FbxVec3Keyframe>& outKeys)
{
    for (const QJsonValue& value : jsonKeys)
    {
        if (!value.isArray())
        {
            continue;
        }
        const QJsonArray key = value.toArray();
        if (key.size() < 4)
        {
            continue;
        }
        motive::animation::FbxVec3Keyframe parsed;
        parsed.timeSeconds = key.at(0).toDouble();
        parsed.value = readVec3KeyValue(key, 1);
        outKeys.push_back(parsed);
    }
    return !outKeys.empty();
}

bool parseQuatKeyArray(const QJsonArray& jsonKeys, std::vector<motive::animation::FbxQuatKeyframe>& outKeys)
{
    for (const QJsonValue& value : jsonKeys)
    {
        if (!value.isArray())
        {
            continue;
        }
        const QJsonArray key = value.toArray();
        motive::animation::FbxQuatKeyframe parsed;
        if (key.size() >= 6)
        {
            parsed.timeSeconds = key.at(0).toDouble();
            parsed.value = glm::normalize(glm::quat(static_cast<float>(key.at(4).toDouble()),
                                                    static_cast<float>(key.at(1).toDouble()),
                                                    static_cast<float>(key.at(2).toDouble()),
                                                    static_cast<float>(key.at(3).toDouble())));
        }
        else if (key.size() >= 4)
        {
            parsed.timeSeconds = key.at(0).toDouble();
            const glm::vec3 eulerDegrees = readVec3KeyValue(key, 1);
            parsed.value = quatFromEulerDegrees(eulerDegrees);
        }
        else
        {
            continue;
        }
        outKeys.push_back(parsed);
    }
    return !outKeys.empty();
}

std::optional<motive::animation::FbxCustomClip> parseCustomClip(const QJsonObject& object,
                                                                const motive::animation::FbxRuntime& runtime)
{
    motive::animation::FbxCustomClip clip;
    clip.name = object.value(QStringLiteral("name")).toString().toStdString();
    clip.durationSeconds = std::max(0.0, object.value(QStringLiteral("durationSeconds")).toDouble(object.value(QStringLiteral("duration")).toDouble()));
    if (clip.name.empty() || clip.durationSeconds <= 0.0)
    {
        std::cerr << "[FBX] Rejecting sidecar clip with missing name/duration"
                  << " name=" << clip.name
                  << " duration=" << clip.durationSeconds
                  << std::endl;
        return std::nullopt;
    }

    const QJsonArray tracks = object.value(QStringLiteral("tracks")).toArray();
    for (const QJsonValue& value : tracks)
    {
        if (!value.isObject())
        {
            continue;
        }
        const QJsonObject trackObject = value.toObject();
        motive::animation::FbxCustomTrack track;
        track.boneName = trackObject.value(QStringLiteral("bone")).toString().toStdString();
        if (track.boneName.empty())
        {
            std::cerr << "[FBX] Ignoring sidecar track with empty bone name in clip "
                      << clip.name << std::endl;
            continue;
        }
        const auto it = runtime.nodeNameToIndex.find(track.boneName);
        if (it == runtime.nodeNameToIndex.end())
        {
            std::cerr << "[FBX] Ignoring sidecar track for unknown bone '"
                      << track.boneName << "' in clip " << clip.name << std::endl;
            continue;
        }
        track.nodeIndex = it->second;
        parseVec3KeyArray(trackObject.value(QStringLiteral("translation")).toArray(), track.translationKeys);
        track.rotationKeysAreAdditive =
            parseQuatKeyArray(trackObject.value(QStringLiteral("rotationEulerDeg")).toArray(), track.rotationKeys);
        parseVec3KeyArray(trackObject.value(QStringLiteral("scale")).toArray(), track.scaleKeys);
        if (track.translationKeys.empty() && track.rotationKeys.empty() && track.scaleKeys.empty())
        {
            std::cerr << "[FBX] Ignoring sidecar track for bone '"
                      << track.boneName << "' in clip " << clip.name
                      << " because it contained no valid keys" << std::endl;
            continue;
        }
        clip.tracks.push_back(std::move(track));
    }

    if (clip.tracks.empty())
    {
        std::cerr << "[FBX] Rejecting sidecar clip '" << clip.name
                  << "' because no valid tracks were loaded" << std::endl;
        return std::nullopt;
    }

    sortClipKeys(clip);
    return clip;
}

bool applyCustomAnimation(Model& model, motive::animation::FbxRuntime& runtime, const motive::animation::FbxClipRuntime& clip);

}  // namespace

namespace motive::animation {

FbxRuntime::~FbxRuntime()
{
    if (scene)
    {
        ufbx_free_scene(scene);
        scene = nullptr;
    }
}

std::unique_ptr<FbxRuntime> createFbxRuntime(ufbx_scene* scene)
{
    auto runtime = std::make_unique<FbxRuntime>();
    runtime->scene = scene;
    if (!scene)
    {
        return runtime;
    }

    runtime->clips.reserve(scene->anim_stacks.count);
    for (size_t i = 0; i < scene->anim_stacks.count; ++i)
    {
        const ufbx_anim_stack* animStack = scene->anim_stacks.data[i];
        if (!animStack || !animStack->anim)
        {
            continue;
        }
        std::string clipName = animStack->name.data && animStack->name.length > 0
            ? std::string(animStack->name.data, animStack->name.length)
            : ("Animation " + std::to_string(i));
        FbxClipRuntime clipRuntime;
        clipRuntime.name = clipName;
        clipRuntime.source = FbxClipRuntime::Source::Scene;
        clipRuntime.anim = animStack->anim;
        clipRuntime.timeBegin = animStack->time_begin;
        clipRuntime.timeEnd = animStack->time_end;
        runtime->clips.push_back(std::move(clipRuntime));
    }

    runtime->nodes.reserve(scene->nodes.count);
    for (size_t i = 0; i < scene->nodes.count; ++i)
    {
        const ufbx_node* node = scene->nodes.data[i];
        runtime->nodes.push_back(node);
        if (node && node->name.data && node->name.length > 0)
        {
            runtime->nodeNameToIndex.emplace(std::string(node->name.data, node->name.length),
                                             static_cast<int>(i));
        }
    }
    if (!runtime->clips.empty())
    {
        runtime->activeClipIndex = 0;
        runtime->timeSeconds = initialPlaybackTime(runtime->clips[0], runtime->playing);
    }
    return runtime;
}

void addFbxMeshBinding(FbxRuntime& runtime, uint32_t meshElementId, std::vector<uint32_t> vertexIndices)
{
    FbxMeshBinding binding;
    binding.meshElementId = meshElementId;
    binding.vertexIndices = std::move(vertexIndices);
    runtime.meshBindings.push_back(std::move(binding));
}

void addFbxMeshBinding(FbxRuntime& runtime,
                       uint32_t meshElementId,
                       uint32_t skinDeformerIndex,
                       std::vector<uint32_t> vertexIndices,
                       std::vector<Vertex> baseVertices,
                       std::vector<glm::uvec4> jointIndices,
                       std::vector<glm::vec4> jointWeights)
{
    FbxMeshBinding binding;
    binding.meshElementId = meshElementId;
    binding.skinDeformerIndex = skinDeformerIndex;
    binding.vertexIndices = std::move(vertexIndices);
    binding.baseVertices = std::move(baseVertices);
    binding.jointIndices = std::move(jointIndices);
    binding.jointWeights = std::move(jointWeights);
    binding.gpuSkinningEligible = !binding.jointIndices.empty() &&
                                  binding.jointIndices.size() == binding.vertexIndices.size() &&
                                  binding.jointWeights.size() == binding.vertexIndices.size();
    runtime.meshBindings.push_back(std::move(binding));
}

void loadFbxSidecarClips(FbxRuntime& runtime, const std::string& fbxPath)
{
    const QFileInfo fbxInfo(QString::fromStdString(fbxPath));
    const QString sidecarPath = fbxInfo.dir().filePath(fbxInfo.completeBaseName() + QStringLiteral(".anim.json"));
    QFile sidecar(sidecarPath);
    if (!sidecar.exists() || !sidecar.open(QIODevice::ReadOnly))
    {
        return;
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(sidecar.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        std::cerr << "[FBX] Ignoring invalid animation sidecar: "
                  << sidecarPath.toStdString()
                  << " parseError=" << parseError.errorString().toStdString()
                  << std::endl;
        return;
    }

    const QJsonArray clips = document.object().value(QStringLiteral("clips")).toArray();
    int loadedClipCount = 0;
    for (const QJsonValue& value : clips)
    {
        if (!value.isObject())
        {
            continue;
        }
        std::optional<FbxCustomClip> clip = parseCustomClip(value.toObject(), runtime);
        if (!clip.has_value())
        {
            continue;
        }

        const int customClipIndex = static_cast<int>(runtime.customClips.size());
        runtime.customClips.push_back(std::move(*clip));

        FbxClipRuntime clipRuntime;
        clipRuntime.name = runtime.customClips.back().name;
        clipRuntime.source = FbxClipRuntime::Source::Custom;
        clipRuntime.timeBegin = 0.0;
        clipRuntime.timeEnd = runtime.customClips.back().durationSeconds;
        clipRuntime.customClipIndex = customClipIndex;
        runtime.clips.push_back(std::move(clipRuntime));
        ++loadedClipCount;
    }

    std::cerr << "[FBX] Sidecar clip load "
              << sidecarPath.toStdString()
              << " requested=" << clips.size()
              << " loaded=" << loadedClipCount
              << std::endl;
}

void setFbxPlaybackState(FbxRuntime& runtime, const std::string& clipName, bool playing, bool loop, float speed)
{
    runtime.playing = playing;
    runtime.loop = loop;
    runtime.speed = speed;

    if (runtime.clips.empty())
    {
        runtime.activeClipIndex = -1;
        runtime.timeSeconds = 0.0;
        return;
    }

    int nextClipIndex = runtime.activeClipIndex;
    if (!clipName.empty())
    {
        for (int i = 0; i < static_cast<int>(runtime.clips.size()); ++i)
        {
            if (runtime.clips[i].name == clipName)
            {
                nextClipIndex = i;
                break;
            }
        }
    }
    if (nextClipIndex < 0)
    {
        nextClipIndex = 0;
    }
    if (nextClipIndex != runtime.activeClipIndex)
    {
        runtime.activeClipIndex = nextClipIndex;
        runtime.timeSeconds = initialPlaybackTime(runtime.clips[nextClipIndex], runtime.playing);
    }
    const ClipPlaybackWindow window = clipPlaybackWindow(runtime.clips[runtime.activeClipIndex], runtime);
    runtime.timeSeconds = clampToPlaybackWindow(runtime.timeSeconds, window);
}

void setFbxPlaybackOptions(FbxRuntime& runtime,
                           bool centroidNormalizationEnabled,
                           float trimStartNormalized,
                           float trimEndNormalized)
{
    runtime.centroidNormalizationEnabled = centroidNormalizationEnabled;
    runtime.trimStartNormalized = std::clamp(trimStartNormalized, 0.0f, 1.0f);
    runtime.trimEndNormalized = std::clamp(trimEndNormalized, 0.0f, 1.0f);
    if (runtime.trimEndNormalized < runtime.trimStartNormalized)
    {
        std::swap(runtime.trimStartNormalized, runtime.trimEndNormalized);
    }

    if (runtime.activeClipIndex >= 0 && runtime.activeClipIndex < static_cast<int>(runtime.clips.size()))
    {
        const ClipPlaybackWindow window = clipPlaybackWindow(runtime.clips[runtime.activeClipIndex], runtime);
        runtime.timeSeconds = clampToPlaybackWindow(runtime.timeSeconds, window);
    }
}

bool applyCustomAnimation(Model& model, FbxRuntime& runtime, const FbxClipRuntime& clip)
{
    if (clip.customClipIndex < 0 || clip.customClipIndex >= static_cast<int>(runtime.customClips.size()))
    {
        return false;
    }

    const FbxCustomClip& customClip = runtime.customClips[clip.customClipIndex];
    if (!runtime.scene || runtime.nodes.empty() || customClip.durationSeconds <= 0.0)
    {
        return false;
    }

    std::vector<glm::mat4> nodeLocal(runtime.nodes.size(), glm::mat4(1.0f));
    std::vector<glm::mat4> nodeWorld(runtime.nodes.size(), glm::mat4(1.0f));
    for (size_t i = 0; i < runtime.nodes.size(); ++i)
    {
        const ufbx_node* node = runtime.nodes[i];
        if (!node)
        {
            continue;
        }
        nodeLocal[i] = toGlmMat4(ufbx_transform_to_matrix(&node->local_transform));
    }

    for (const FbxCustomTrack& track : customClip.tracks)
    {
        if (track.nodeIndex < 0 || track.nodeIndex >= static_cast<int>(runtime.nodes.size()))
        {
            continue;
        }
        const ufbx_node* node = runtime.nodes[static_cast<size_t>(track.nodeIndex)];
        if (!node)
        {
            continue;
        }

        glm::vec3 translation(static_cast<float>(node->local_transform.translation.x),
                              static_cast<float>(node->local_transform.translation.y),
                              static_cast<float>(node->local_transform.translation.z));
        glm::vec3 scale(static_cast<float>(node->local_transform.scale.x),
                        static_cast<float>(node->local_transform.scale.y),
                        static_cast<float>(node->local_transform.scale.z));
        glm::quat rotation(static_cast<float>(node->local_transform.rotation.w),
                           static_cast<float>(node->local_transform.rotation.x),
                           static_cast<float>(node->local_transform.rotation.y),
                           static_cast<float>(node->local_transform.rotation.z));
        rotation = glm::normalize(rotation);

        translation = sampleKeyframes<FbxVec3Keyframe, glm::vec3>(
            track.translationKeys,
            runtime.timeSeconds,
            translation,
            [](const glm::vec3& a, const glm::vec3& b, float t) { return glm::mix(a, b, t); });
        scale = sampleKeyframes<FbxVec3Keyframe, glm::vec3>(
            track.scaleKeys,
            runtime.timeSeconds,
            scale,
            [](const glm::vec3& a, const glm::vec3& b, float t) { return glm::mix(a, b, t); });
        if (!track.rotationKeys.empty())
        {
            const glm::quat sampledRotation = sampleKeyframes<FbxQuatKeyframe, glm::quat>(
                track.rotationKeys,
                runtime.timeSeconds,
                track.rotationKeysAreAdditive ? glm::quat(1.0f, 0.0f, 0.0f, 0.0f) : rotation,
                [](const glm::quat& a, const glm::quat& b, float t) {
                    return glm::normalize(glm::slerp(a, b, t));
                });
            rotation = track.rotationKeysAreAdditive
                ? glm::normalize(rotation * sampledRotation)
                : sampledRotation;
        }

        nodeLocal[static_cast<size_t>(track.nodeIndex)] =
            glm::translate(glm::mat4(1.0f), translation) *
            glm::toMat4(rotation) *
            glm::scale(glm::mat4(1.0f), scale);
    }

    for (size_t i = 0; i < runtime.nodes.size(); ++i)
    {
        const ufbx_node* node = runtime.nodes[i];
        if (!node)
        {
            continue;
        }

        glm::mat4 world = nodeLocal[i];
        if (node->parent)
        {
            const auto parentIt = runtime.nodeNameToIndex.find(std::string(node->parent->name.data, node->parent->name.length));
            if (parentIt != runtime.nodeNameToIndex.end())
            {
                world = nodeWorld[static_cast<size_t>(parentIt->second)] * world;
            }
        }
        nodeWorld[i] = world;
    }

    const size_t meshCount = std::min(model.meshes.size(), runtime.meshBindings.size());
    const bool forceCpuNormalizedPose =
        model.character.isControllable && runtime.centroidNormalizationEnabled;
    bool updated = false;
    for (size_t meshIndex = 0; meshIndex < meshCount; ++meshIndex)
    {
        const FbxMeshBinding& binding = runtime.meshBindings[meshIndex];
        Primitive* primitive = !model.meshes[meshIndex].primitives.empty()
            ? model.meshes[meshIndex].primitives.front().get()
            : nullptr;
        if (!primitive)
        {
            continue;
        }

        const ufbx_mesh* mesh = findMeshByElementId(runtime.scene, binding.meshElementId);
        if (!mesh)
        {
            continue;
        }
        const ufbx_skin_deformer* skin = binding.skinDeformerIndex < mesh->skin_deformers.count
            ? mesh->skin_deformers.data[binding.skinDeformerIndex]
            : nullptr;
        if (!skin)
        {
            continue;
        }

        std::vector<glm::mat4> jointMatrices;
        jointMatrices.reserve(std::min<size_t>(skin->clusters.count, kMaxPrimitiveSkinJoints));
        for (size_t clusterIndex = 0; clusterIndex < skin->clusters.count && clusterIndex < kMaxPrimitiveSkinJoints; ++clusterIndex)
        {
            const ufbx_skin_cluster* cluster = skin->clusters.data[clusterIndex];
            if (!cluster || !cluster->bone_node)
            {
                jointMatrices.push_back(glm::mat4(1.0f));
                continue;
            }

            const auto it = runtime.nodeNameToIndex.find(std::string(cluster->bone_node->name.data, cluster->bone_node->name.length));
            if (it == runtime.nodeNameToIndex.end())
            {
                jointMatrices.push_back(glm::mat4(1.0f));
                continue;
            }

            const glm::mat4 boneWorld = nodeWorld[static_cast<size_t>(it->second)];
            jointMatrices.push_back(boneWorld * toGlmMat4(cluster->geometry_to_bone));
        }

        if (!forceCpuNormalizedPose &&
            primitive->gpuSkinningEnabled &&
            binding.gpuSkinningEligible &&
            !binding.baseVertices.empty())
        {
            primitive->updateSkinningMatrices(jointMatrices);
            updated = true;
            continue;
        }

        if (binding.baseVertices.empty() ||
            binding.jointIndices.size() != binding.baseVertices.size() ||
            binding.jointWeights.size() != binding.baseVertices.size())
        {
            continue;
        }

        std::vector<Vertex> vertices = binding.baseVertices;
        for (size_t vertexIndex = 0; vertexIndex < vertices.size(); ++vertexIndex)
        {
            const Vertex& base = binding.baseVertices[vertexIndex];
            glm::vec4 skinnedPos(0.0f);
            glm::vec3 skinnedNormal(0.0f);
            float totalWeight = 0.0f;
            for (int influence = 0; influence < 4; ++influence)
            {
                const uint32_t jointIndex = binding.jointIndices[vertexIndex][influence];
                const float weight = binding.jointWeights[vertexIndex][influence];
                if (weight <= 1e-6f || jointIndex >= jointMatrices.size())
                {
                    continue;
                }
                const glm::mat4& joint = jointMatrices[jointIndex];
                skinnedPos += joint * glm::vec4(base.pos, 1.0f) * weight;
                skinnedNormal += glm::mat3(joint) * base.normal * weight;
                totalWeight += weight;
            }
            if (totalWeight > 1e-6f)
            {
                vertices[vertexIndex].pos = glm::vec3(skinnedPos) / totalWeight;
                vertices[vertexIndex].normal = glm::normalize(skinnedNormal / totalWeight);
            }
        }

        if (runtime.centroidNormalizationEnabled)
        {
            normalizeControllableCharacterRootOffset(model, vertices);
        }
        primitive->updateVertexData(vertices);
        updated = true;
    }

    if (updated)
    {
        model.recomputeBounds();
    }
    return updated;
}

bool updateFbxAnimation(Model& model, FbxRuntime& runtime, double deltaSeconds)
{
    if (!runtime.scene || runtime.activeClipIndex < 0 || runtime.activeClipIndex >= static_cast<int>(runtime.clips.size()))
    {
        return false;
    }

    const FbxClipRuntime& clip = runtime.clips[runtime.activeClipIndex];
    const ClipPlaybackWindow window = clipPlaybackWindow(clip, runtime);
    if (runtime.playing && window.duration > 0.0)
    {
        runtime.timeSeconds += deltaSeconds * static_cast<double>(runtime.speed);
        if (runtime.loop)
        {
            while (runtime.timeSeconds > window.end)
            {
                runtime.timeSeconds -= window.duration;
            }
            while (runtime.timeSeconds < window.start)
            {
                runtime.timeSeconds += window.duration;
            }
        }
        else
        {
            runtime.timeSeconds = clampToPlaybackWindow(runtime.timeSeconds, window);
        }
    }
    else
    {
        runtime.timeSeconds = clampToPlaybackWindow(runtime.timeSeconds, window);
    }

    if (clip.source == FbxClipRuntime::Source::Custom)
    {
        return applyCustomAnimation(model, runtime, clip);
    }

    const size_t meshCount = std::min(model.meshes.size(), runtime.meshBindings.size());
    const bool forceCpuNormalizedPose =
        model.character.isControllable && runtime.centroidNormalizationEnabled;
    bool allMeshesUseGpuSkinning = meshCount > 0 && !forceCpuNormalizedPose;
    for (size_t meshIndex = 0; meshIndex < meshCount; ++meshIndex)
    {
        const FbxMeshBinding& binding = runtime.meshBindings[meshIndex];
        Primitive* primitive = !model.meshes[meshIndex].primitives.empty()
            ? model.meshes[meshIndex].primitives.front().get()
            : nullptr;
        if (!primitive || !primitive->gpuSkinningEnabled || !binding.gpuSkinningEligible)
        {
            allMeshesUseGpuSkinning = false;
            break;
        }
    }

    ufbx_evaluate_opts evalOpts = {};
    // CPU skin evaluation is expensive for high-density FBX characters. When all
    // primitives can skin on the GPU, only evaluate animated node/joint matrices.
    evalOpts.evaluate_skinning = !allMeshesUseGpuSkinning;
    ufbx_error error;
    ufbx_scene* evaluatedScene = ufbx_evaluate_scene(runtime.scene, clip.anim, runtime.timeSeconds, &evalOpts, &error);
    if (!evaluatedScene)
    {
        return false;
    }

    std::vector<std::vector<Vertex>> normalizedPoseVertices;
    glm::vec3 globalCenterDelta(0.0f);
    if (forceCpuNormalizedPose)
    {
        normalizedPoseVertices.resize(meshCount);
        glm::vec3 globalMin(std::numeric_limits<float>::max());
        glm::vec3 globalMax(std::numeric_limits<float>::lowest());
        bool foundVertex = false;

        for (size_t meshIndex = 0; meshIndex < meshCount; ++meshIndex)
        {
            const FbxMeshBinding& binding = runtime.meshBindings[meshIndex];
            const ufbx_mesh* evaluatedMesh = findMeshByElementId(evaluatedScene, binding.meshElementId);
            if (!evaluatedMesh)
            {
                continue;
            }

            normalizedPoseVertices[meshIndex] = buildAnimatedVerticesFromFbxMesh(evaluatedMesh, binding.vertexIndices);
            for (const Vertex& vertex : normalizedPoseVertices[meshIndex])
            {
                globalMin = glm::min(globalMin, vertex.pos);
                globalMax = glm::max(globalMax, vertex.pos);
                foundVertex = true;
            }
        }

        if (foundVertex && model.followAnchorLocalCenterInitialized)
        {
            const glm::vec3 animatedCenter = (globalMin + globalMax) * 0.5f;
            globalCenterDelta = animatedCenter - model.followAnchorLocalCenter;
            if (glm::length(globalCenterDelta) > 1e-6f)
            {
                for (std::vector<Vertex>& vertices : normalizedPoseVertices)
                {
                    for (Vertex& vertex : vertices)
                    {
                        vertex.pos -= globalCenterDelta;
                    }
                }
            }
        }
    }

    bool updated = false;
    for (size_t meshIndex = 0; meshIndex < meshCount; ++meshIndex)
    {
        const FbxMeshBinding& binding = runtime.meshBindings[meshIndex];
        const ufbx_mesh* evaluatedMesh = findMeshByElementId(evaluatedScene, binding.meshElementId);
        if (!evaluatedMesh || model.meshes[meshIndex].primitives.empty() || !model.meshes[meshIndex].primitives.front())
        {
            continue;
        }

        Primitive* primitive = model.meshes[meshIndex].primitives.front().get();
        if (!primitive)
        {
            continue;
        }

        if (!forceCpuNormalizedPose && primitive->gpuSkinningEnabled && binding.gpuSkinningEligible)
        {
            const ufbx_skin_deformer* skin = binding.skinDeformerIndex < evaluatedMesh->skin_deformers.count
                ? evaluatedMesh->skin_deformers.data[binding.skinDeformerIndex]
                : nullptr;
            const ufbx_node* meshNode = evaluatedMesh->instances.count > 0 ? evaluatedMesh->instances.data[0] : nullptr;
            if (skin && meshNode)
            {
                glm::mat4 invGeometryToWorld = glm::inverse(toGlmMat4(meshNode->geometry_to_world));
                std::vector<glm::mat4> jointMatrices;
                jointMatrices.reserve(std::min<size_t>(skin->clusters.count, kMaxPrimitiveSkinJoints));
                for (size_t clusterIndex = 0; clusterIndex < skin->clusters.count && clusterIndex < kMaxPrimitiveSkinJoints; ++clusterIndex)
                {
                    const ufbx_skin_cluster* cluster = skin->clusters.data[clusterIndex];
                    if (!cluster)
                    {
                        jointMatrices.push_back(glm::mat4(1.0f));
                        continue;
                    }
                    jointMatrices.push_back(invGeometryToWorld * toGlmMat4(cluster->geometry_to_world));
                }
                primitive->updateSkinningMatrices(jointMatrices);
                updated = true;
                continue;
            }
        }

        std::vector<Vertex> vertices = forceCpuNormalizedPose
            ? std::move(normalizedPoseVertices[meshIndex])
            : buildAnimatedVerticesFromFbxMesh(evaluatedMesh, binding.vertexIndices);
        if (!forceCpuNormalizedPose && runtime.centroidNormalizationEnabled)
        {
            normalizeControllableCharacterRootOffset(model, vertices);
        }
        if (vertices.empty())
        {
            continue;
        }

        if (binding.gpuSkinningEligible && !forceCpuNormalizedPose)
        {
            const size_t count = std::min(vertices.size(), binding.jointIndices.size());
            for (size_t i = 0; i < count; ++i)
            {
                vertices[i].joints = binding.jointIndices[i];
                vertices[i].weights = binding.jointWeights[i];
            }
        }

        primitive->updateVertexData(vertices);
        updated = true;
    }

    ufbx_free_scene(evaluatedScene);
    if (updated)
    {
        model.recomputeBounds();
    }
    return updated;
}

}  // namespace motive::animation
