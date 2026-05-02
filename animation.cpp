#include "animation.h"

#include "model.h"
#include "ufbx.h"

#include <algorithm>
#include <cmath>
#include <limits>

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
        runtime->clips.push_back(FbxClipRuntime{clipName, animStack->anim, animStack->time_begin, animStack->time_end});
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
    runtime.meshBindings.push_back(FbxMeshBinding{meshElementId, std::move(vertexIndices)});
}

void addFbxMeshBinding(FbxRuntime& runtime,
                       uint32_t meshElementId,
                       uint32_t skinDeformerIndex,
                       std::vector<uint32_t> vertexIndices,
                       std::vector<glm::uvec4> jointIndices,
                       std::vector<glm::vec4> jointWeights)
{
    FbxMeshBinding binding;
    binding.meshElementId = meshElementId;
    binding.skinDeformerIndex = skinDeformerIndex;
    binding.vertexIndices = std::move(vertexIndices);
    binding.jointIndices = std::move(jointIndices);
    binding.jointWeights = std::move(jointWeights);
    binding.gpuSkinningEligible = !binding.jointIndices.empty() &&
                                  binding.jointIndices.size() == binding.vertexIndices.size() &&
                                  binding.jointWeights.size() == binding.vertexIndices.size();
    runtime.meshBindings.push_back(std::move(binding));
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

    const size_t meshCount = std::min(model.meshes.size(), runtime.meshBindings.size());
    bool allMeshesUseGpuSkinning = meshCount > 0;
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

        if (primitive->gpuSkinningEnabled && binding.gpuSkinningEligible)
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

        std::vector<Vertex> vertices = buildAnimatedVerticesFromFbxMesh(evaluatedMesh, binding.vertexIndices);
        const glm::vec3 centerDelta = runtime.centroidNormalizationEnabled
            ? normalizeControllableCharacterRootOffset(model, vertices)
            : glm::vec3(0.0f);
        if (vertices.empty())
        {
            continue;
        }

        if (binding.gpuSkinningEligible)
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
