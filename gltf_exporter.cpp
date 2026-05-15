#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif

#include "gltf_exporter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "fbx_import.h"
#include "ufbx.h"

namespace {

struct BufferBuilder
{
    std::vector<unsigned char> data;

    void align(size_t alignment)
    {
        const size_t remainder = data.size() % alignment;
        if (remainder == 0)
        {
            return;
        }
        data.resize(data.size() + (alignment - remainder), 0u);
    }

    template <typename T>
    size_t appendSpan(const std::vector<T>& values, size_t alignment = 4)
    {
        align(alignment);
        const size_t offset = data.size();
        if (!values.empty())
        {
            const size_t bytes = sizeof(T) * values.size();
            data.resize(offset + bytes);
            std::memcpy(data.data() + offset, values.data(), bytes);
        }
        return offset;
    }
};

struct ExportTrackVec3
{
    std::vector<float> times;
    std::vector<glm::vec3> values;
};

struct ExportTrackQuat
{
    std::vector<float> times;
    std::vector<glm::quat> values;
};

struct ExportClip
{
    std::string name;
    std::unordered_map<std::string, ExportTrackVec3> translationTracks;
    std::unordered_map<std::string, ExportTrackQuat> rotationTracks;
    std::unordered_map<std::string, ExportTrackVec3> scaleTracks;
};

struct ExportMaterial
{
    bool hasBaseColorTexture = false;
    QImage baseColorTexture;
    glm::vec4 baseColorFactor = glm::vec4(1.0f);
    std::string name;
};

struct PrimitiveBuildData
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    ExportMaterial material;
};

glm::mat4 toGlmMat4(const ufbx_matrix& value)
{
    glm::mat4 result(1.0f);
    result[0][0] = static_cast<float>(value.m00); result[0][1] = static_cast<float>(value.m10); result[0][2] = static_cast<float>(value.m20); result[0][3] = 0.0f;
    result[1][0] = static_cast<float>(value.m01); result[1][1] = static_cast<float>(value.m11); result[1][2] = static_cast<float>(value.m21); result[1][3] = 0.0f;
    result[2][0] = static_cast<float>(value.m02); result[2][1] = static_cast<float>(value.m12); result[2][2] = static_cast<float>(value.m22); result[2][3] = 0.0f;
    result[3][0] = static_cast<float>(value.m03); result[3][1] = static_cast<float>(value.m13); result[3][2] = static_cast<float>(value.m23); result[3][3] = 1.0f;
    return result;
}

glm::quat quatFromEulerDegrees(const glm::vec3& degrees)
{
    return glm::normalize(glm::quat(glm::radians(degrees)));
}

const ufbx_material* materialForFbxMesh(const ufbx_mesh* mesh, int materialIndex)
{
    if (!mesh || !mesh->materials.data || mesh->materials.count == 0)
    {
        return nullptr;
    }
    const int clamped = materialIndex >= 0 && materialIndex < static_cast<int>(mesh->materials.count)
        ? materialIndex
        : 0;
    return mesh->materials.data[clamped];
}

std::optional<ExportMaterial> exportMaterialForFbxMesh(const ufbx_mesh* mesh,
                                                       int materialIndex,
                                                       const QString& sourceDirectory)
{
    ExportMaterial material;
    glm::vec4 color(1.0f);
    extractFbxMaterialColor(mesh, color, materialIndex);
    material.baseColorFactor = color;

    const ufbx_material* sourceMaterial = materialForFbxMesh(mesh, materialIndex);
    if (sourceMaterial && sourceMaterial->name.data && sourceMaterial->name.length > 0)
    {
        material.name.assign(sourceMaterial->name.data, sourceMaterial->name.length);
    }

    const ufbx_texture* texture = nullptr;
    if (sourceMaterial)
    {
        if (sourceMaterial->pbr.base_color.texture)
        {
            texture = sourceMaterial->pbr.base_color.texture;
        }
        else if (sourceMaterial->fbx.diffuse_color.texture)
        {
            texture = sourceMaterial->fbx.diffuse_color.texture;
        }
    }

    if (texture)
    {
        QImage image;
        if (loadImageFromUfbxTexture(texture, sourceDirectory, image, nullptr) && !image.isNull())
        {
            material.hasBaseColorTexture = true;
            material.baseColorTexture = image.convertToFormat(QImage::Format_RGBA8888);
        }
    }

    return material;
}

PrimitiveBuildData buildPrimitiveData(const ufbx_mesh* mesh,
                                      const std::optional<ufbx_mesh_part>& part,
                                      const QString& sourceDirectory)
{
    PrimitiveBuildData data;
    std::vector<uint32_t> sourceIndices;
    if (part.has_value())
    {
        data.vertices = buildVerticesFromFbxMeshPart(mesh, *part, &sourceIndices);
    }
    else
    {
        data.vertices = buildVerticesFromFbxMesh(mesh, &sourceIndices);
    }

    data.indices.resize(data.vertices.size());
    for (uint32_t i = 0; i < data.indices.size(); ++i)
    {
        data.indices[i] = i;
        if (i < sourceIndices.size())
        {
            glm::uvec4 joints(0u);
            glm::vec4 weights(0.0f);
            getFbxSkinInfluences(mesh, sourceIndices[i], joints, weights, nullptr);
            data.vertices[i].joints = joints;
            data.vertices[i].weights = weights;
        }
    }

    const int materialIndex = part.has_value()
        ? materialIndexForFbxMeshPart(mesh, *part)
        : 0;
    const std::optional<ExportMaterial> material = exportMaterialForFbxMesh(mesh, materialIndex, sourceDirectory);
    if (material.has_value())
    {
        data.material = *material;
    }
    return data;
}

glm::vec3 minVec3(const glm::vec3& a, const glm::vec3& b)
{
    return glm::vec3(std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z));
}

glm::vec3 maxVec3(const glm::vec3& a, const glm::vec3& b)
{
    return glm::vec3(std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z));
}

template <typename T>
int appendBufferViewAndAccessor(tinygltf::Model& model,
                                BufferBuilder& builder,
                                const std::vector<T>& values,
                                int componentType,
                                int type,
                                int target,
                                const std::optional<glm::vec3>& minVec = std::nullopt,
                                const std::optional<glm::vec3>& maxVec = std::nullopt)
{
    if (values.empty())
    {
        return -1;
    }

    const size_t byteOffset = builder.appendSpan(values);

    tinygltf::BufferView view;
    view.buffer = 0;
    view.byteOffset = byteOffset;
    view.byteLength = sizeof(T) * values.size();
    view.target = target;
    model.bufferViews.push_back(view);
    const int bufferViewIndex = static_cast<int>(model.bufferViews.size()) - 1;

    tinygltf::Accessor accessor;
    accessor.bufferView = bufferViewIndex;
    accessor.byteOffset = 0;
    accessor.componentType = componentType;
    accessor.count = values.size();
    accessor.type = type;
    if (minVec.has_value())
    {
        accessor.minValues = {minVec->x, minVec->y, minVec->z};
    }
    if (maxVec.has_value())
    {
        accessor.maxValues = {maxVec->x, maxVec->y, maxVec->z};
    }
    model.accessors.push_back(accessor);
    return static_cast<int>(model.accessors.size()) - 1;
}

int appendScalarAccessor(tinygltf::Model& model,
                         BufferBuilder& builder,
                         const std::vector<float>& values)
{
    if (values.empty())
    {
        return -1;
    }
    const auto minmax = std::minmax_element(values.begin(), values.end());
    const size_t byteOffset = builder.appendSpan(values);

    tinygltf::BufferView view;
    view.buffer = 0;
    view.byteOffset = byteOffset;
    view.byteLength = sizeof(float) * values.size();
    model.bufferViews.push_back(view);
    const int viewIndex = static_cast<int>(model.bufferViews.size()) - 1;

    tinygltf::Accessor accessor;
    accessor.bufferView = viewIndex;
    accessor.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
    accessor.count = values.size();
    accessor.type = TINYGLTF_TYPE_SCALAR;
    accessor.minValues = {static_cast<double>(*minmax.first)};
    accessor.maxValues = {static_cast<double>(*minmax.second)};
    model.accessors.push_back(accessor);
    return static_cast<int>(model.accessors.size()) - 1;
}

bool parseVec3Track(const QJsonArray& source, ExportTrackVec3& track)
{
    for (const QJsonValue& value : source)
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
        track.times.push_back(static_cast<float>(key.at(0).toDouble()));
        track.values.push_back(glm::vec3(static_cast<float>(key.at(1).toDouble()),
                                         static_cast<float>(key.at(2).toDouble()),
                                         static_cast<float>(key.at(3).toDouble())));
    }
    return !track.times.empty() && track.times.size() == track.values.size();
}

bool parseQuatTrack(const QJsonArray& source, ExportTrackQuat& track)
{
    for (const QJsonValue& value : source)
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
        track.times.push_back(static_cast<float>(key.at(0).toDouble()));
        if (key.size() >= 4)
        {
            track.values.push_back(quatFromEulerDegrees(glm::vec3(static_cast<float>(key.at(1).toDouble()),
                                                                  static_cast<float>(key.at(2).toDouble()),
                                                                  static_cast<float>(key.at(3).toDouble()))));
        }
        else
        {
            track.values.push_back(glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
        }
    }
    return !track.times.empty() && track.times.size() == track.values.size();
}

std::vector<ExportClip> loadSidecarClips(const QString& fbxPath)
{
    const QFileInfo info(fbxPath);
    const QString sidecarPath = info.dir().filePath(info.completeBaseName() + QStringLiteral(".anim.json"));
    QFile file(sidecarPath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly))
    {
        return {};
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        return {};
    }

    std::vector<ExportClip> clips;
    const QJsonArray jsonClips = document.object().value(QStringLiteral("clips")).toArray();
    for (const QJsonValue& clipValue : jsonClips)
    {
        if (!clipValue.isObject())
        {
            continue;
        }
        const QJsonObject clipObject = clipValue.toObject();
        ExportClip clip;
        clip.name = clipObject.value(QStringLiteral("name")).toString().toStdString();
        const QJsonArray tracks = clipObject.value(QStringLiteral("tracks")).toArray();
        for (const QJsonValue& trackValue : tracks)
        {
            if (!trackValue.isObject())
            {
                continue;
            }
            const QJsonObject trackObject = trackValue.toObject();
            const std::string boneName = trackObject.value(QStringLiteral("bone")).toString().toStdString();
            if (boneName.empty())
            {
                continue;
            }

            ExportTrackVec3 translationTrack;
            if (parseVec3Track(trackObject.value(QStringLiteral("translation")).toArray(), translationTrack))
            {
                clip.translationTracks.emplace(boneName, std::move(translationTrack));
            }

            ExportTrackQuat rotationTrack;
            if (parseQuatTrack(trackObject.value(QStringLiteral("rotationEulerDeg")).toArray(), rotationTrack))
            {
                clip.rotationTracks.emplace(boneName, std::move(rotationTrack));
            }

            ExportTrackVec3 scaleTrack;
            if (parseVec3Track(trackObject.value(QStringLiteral("scale")).toArray(), scaleTrack))
            {
                clip.scaleTracks.emplace(boneName, std::move(scaleTrack));
            }
        }
        if (!clip.name.empty())
        {
            clips.push_back(std::move(clip));
        }
    }
    return clips;
}

void addImageMaterial(tinygltf::Model& model,
                      const ExportMaterial& material,
                      int& outMaterialIndex)
{
    tinygltf::Material gltfMaterial;
    gltfMaterial.name = material.name;
    gltfMaterial.pbrMetallicRoughness.baseColorFactor = {
        material.baseColorFactor.r,
        material.baseColorFactor.g,
        material.baseColorFactor.b,
        material.baseColorFactor.a
    };
    gltfMaterial.pbrMetallicRoughness.metallicFactor = 0.0;
    gltfMaterial.pbrMetallicRoughness.roughnessFactor = 1.0;
    gltfMaterial.doubleSided = true;

    if (material.hasBaseColorTexture && !material.baseColorTexture.isNull())
    {
        tinygltf::Image image;
        image.width = material.baseColorTexture.width();
        image.height = material.baseColorTexture.height();
        image.component = 4;
        image.bits = 8;
        image.pixel_type = TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE;
        image.image.resize(static_cast<size_t>(material.baseColorTexture.sizeInBytes()));
        std::memcpy(image.image.data(), material.baseColorTexture.constBits(), image.image.size());
        model.images.push_back(std::move(image));

        tinygltf::Sampler sampler;
        sampler.minFilter = TINYGLTF_TEXTURE_FILTER_LINEAR;
        sampler.magFilter = TINYGLTF_TEXTURE_FILTER_LINEAR;
        sampler.wrapS = TINYGLTF_TEXTURE_WRAP_REPEAT;
        sampler.wrapT = TINYGLTF_TEXTURE_WRAP_REPEAT;
        model.samplers.push_back(sampler);
        const int samplerIndex = static_cast<int>(model.samplers.size()) - 1;

        tinygltf::Texture texture;
        texture.source = static_cast<int>(model.images.size()) - 1;
        texture.sampler = samplerIndex;
        model.textures.push_back(texture);

        gltfMaterial.pbrMetallicRoughness.baseColorTexture.index =
            static_cast<int>(model.textures.size()) - 1;
    }

    model.materials.push_back(std::move(gltfMaterial));
    outMaterialIndex = static_cast<int>(model.materials.size()) - 1;
}

} // namespace

namespace motive::exporter {

bool exportFbxAssetToGltf(const QString& sourceFbxPath,
                          const QString& outputPath,
                          QString* errorMessage)
{
    const QFileInfo sourceInfo(sourceFbxPath);
    if (!sourceInfo.exists())
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("Source FBX does not exist.");
        }
        return false;
    }

    ufbx_load_opts opts = {};
    opts.generate_missing_normals = true;
    ufbx_error error;
    ufbx_scene* scene = ufbx_load_file(sourceFbxPath.toUtf8().constData(), &opts, &error);
    if (!scene)
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("FBX load failed: %1")
                .arg(QString::fromUtf8(error.description.data, static_cast<qsizetype>(error.description.length)));
        }
        return false;
    }

    tinygltf::Model model;
    model.asset.version = "2.0";
    model.asset.generator = "Motive3D";
    model.defaultScene = 0;
    model.scenes.resize(1);
    model.buffers.resize(1);

    BufferBuilder builder;
    const QString sourceDirectory = sourceInfo.absolutePath();

    std::unordered_map<const ufbx_node*, int> nodeToIndex;
    std::unordered_map<std::string, int> nodeNameToIndex;
    model.nodes.resize(scene->nodes.count);
    for (size_t i = 0; i < scene->nodes.count; ++i)
    {
        const ufbx_node* node = scene->nodes.data[i];
        nodeToIndex.emplace(node, static_cast<int>(i));
        if (node && node->name.data && node->name.length > 0)
        {
            nodeNameToIndex.emplace(std::string(node->name.data, node->name.length), static_cast<int>(i));
        }
    }

    for (size_t i = 0; i < scene->nodes.count; ++i)
    {
        const ufbx_node* node = scene->nodes.data[i];
        tinygltf::Node gltfNode;
        if (node->name.data && node->name.length > 0)
        {
            gltfNode.name.assign(node->name.data, node->name.length);
        }
        gltfNode.translation = {
            node->local_transform.translation.x,
            node->local_transform.translation.y,
            node->local_transform.translation.z
        };
        gltfNode.rotation = {
            node->local_transform.rotation.x,
            node->local_transform.rotation.y,
            node->local_transform.rotation.z,
            node->local_transform.rotation.w
        };
        gltfNode.scale = {
            node->local_transform.scale.x,
            node->local_transform.scale.y,
            node->local_transform.scale.z
        };
        for (size_t childIndex = 0; childIndex < node->children.count; ++childIndex)
        {
            const ufbx_node* child = node->children.data[childIndex];
                auto it = nodeToIndex.find(child);
            if (it != nodeToIndex.end())
            {
                gltfNode.children.push_back(it->second);
            }
        }
        model.nodes[i] = std::move(gltfNode);
    }

    for (size_t i = 0; i < scene->nodes.count; ++i)
    {
        const ufbx_node* node = scene->nodes.data[i];
        if (node && !node->parent)
        {
            model.scenes[0].nodes.push_back(static_cast<int>(i));
        }
    }

    std::unordered_map<const ufbx_mesh*, int> meshToIndex;
    std::unordered_map<const ufbx_mesh*, int> skinToIndex;

    for (size_t meshIndex = 0; meshIndex < scene->meshes.count; ++meshIndex)
    {
        const ufbx_mesh* mesh = scene->meshes.data[meshIndex];
        if (!mesh)
        {
            continue;
        }

        tinygltf::Mesh gltfMesh;
        if (mesh->name.data && mesh->name.length > 0)
        {
            gltfMesh.name.assign(mesh->name.data, mesh->name.length);
        }

        std::vector<PrimitiveBuildData> primitiveDatas;
        if (mesh->material_parts.count > 1)
        {
            primitiveDatas.reserve(mesh->material_parts.count);
            for (size_t partIndex = 0; partIndex < mesh->material_parts.count; ++partIndex)
            {
                primitiveDatas.push_back(buildPrimitiveData(mesh,
                                                            mesh->material_parts.data[partIndex],
                                                            sourceDirectory));
            }
        }
        else
        {
            primitiveDatas.push_back(buildPrimitiveData(mesh, std::nullopt, sourceDirectory));
        }

        for (const PrimitiveBuildData& primitiveData : primitiveDatas)
        {
            if (primitiveData.vertices.empty() || primitiveData.indices.empty())
            {
                continue;
            }

            std::vector<glm::vec3> positions;
            std::vector<glm::vec3> normals;
            std::vector<glm::vec2> texcoords;
            std::vector<glm::uvec4> joints;
            std::vector<glm::vec4> weights;
            positions.reserve(primitiveData.vertices.size());
            normals.reserve(primitiveData.vertices.size());
            texcoords.reserve(primitiveData.vertices.size());
            joints.reserve(primitiveData.vertices.size());
            weights.reserve(primitiveData.vertices.size());

            glm::vec3 minPos(std::numeric_limits<float>::max());
            glm::vec3 maxPos(std::numeric_limits<float>::lowest());
            for (const Vertex& vertex : primitiveData.vertices)
            {
                positions.push_back(vertex.pos);
                normals.push_back(vertex.normal);
                texcoords.push_back(vertex.texCoord);
                joints.push_back(vertex.joints);
                weights.push_back(vertex.weights);
                minPos = minVec3(minPos, vertex.pos);
                maxPos = maxVec3(maxPos, vertex.pos);
            }

            tinygltf::Primitive primitive;
            primitive.mode = TINYGLTF_MODE_TRIANGLES;
            primitive.attributes["POSITION"] = appendBufferViewAndAccessor(
                model, builder, positions,
                TINYGLTF_COMPONENT_TYPE_FLOAT, TINYGLTF_TYPE_VEC3,
                TINYGLTF_TARGET_ARRAY_BUFFER, minPos, maxPos);
            primitive.attributes["NORMAL"] = appendBufferViewAndAccessor(
                model, builder, normals,
                TINYGLTF_COMPONENT_TYPE_FLOAT, TINYGLTF_TYPE_VEC3,
                TINYGLTF_TARGET_ARRAY_BUFFER);
            primitive.attributes["TEXCOORD_0"] = appendBufferViewAndAccessor(
                model, builder, texcoords,
                TINYGLTF_COMPONENT_TYPE_FLOAT, TINYGLTF_TYPE_VEC2,
                TINYGLTF_TARGET_ARRAY_BUFFER);
            primitive.attributes["JOINTS_0"] = appendBufferViewAndAccessor(
                model, builder, joints,
                TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT, TINYGLTF_TYPE_VEC4,
                TINYGLTF_TARGET_ARRAY_BUFFER);
            primitive.attributes["WEIGHTS_0"] = appendBufferViewAndAccessor(
                model, builder, weights,
                TINYGLTF_COMPONENT_TYPE_FLOAT, TINYGLTF_TYPE_VEC4,
                TINYGLTF_TARGET_ARRAY_BUFFER);

            primitive.indices = appendBufferViewAndAccessor(
                model, builder, primitiveData.indices,
                TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT, TINYGLTF_TYPE_SCALAR,
                TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER);

            int materialIndex = -1;
            addImageMaterial(model, primitiveData.material, materialIndex);
            primitive.material = materialIndex;
            gltfMesh.primitives.push_back(std::move(primitive));
        }

        model.meshes.push_back(std::move(gltfMesh));
        const int gltfMeshIndex = static_cast<int>(model.meshes.size()) - 1;
        meshToIndex.emplace(mesh, gltfMeshIndex);

        if (mesh->skin_deformers.count > 0 && mesh->skin_deformers.data[0])
        {
            const ufbx_skin_deformer* skin = mesh->skin_deformers.data[0];
            tinygltf::Skin gltfSkin;
            gltfSkin.name = model.meshes[gltfMeshIndex].name;

            std::vector<glm::mat4> inverseBindMatrices;
            inverseBindMatrices.reserve(std::min<size_t>(skin->clusters.count, kMaxPrimitiveSkinJoints));
            const ufbx_node* skeletonRoot = nullptr;

            for (size_t clusterIndex = 0; clusterIndex < skin->clusters.count && clusterIndex < kMaxPrimitiveSkinJoints; ++clusterIndex)
            {
                const ufbx_skin_cluster* cluster = skin->clusters.data[clusterIndex];
                if (!cluster || !cluster->bone_node)
                {
                    continue;
                }

                auto nodeIt = nodeToIndex.find(cluster->bone_node);
                if (nodeIt == nodeToIndex.end())
                {
                    continue;
                }

                gltfSkin.joints.push_back(nodeIt->second);
                inverseBindMatrices.push_back(toGlmMat4(cluster->geometry_to_bone));

                const ufbx_node* candidate = cluster->bone_node;
                while (candidate && candidate->parent && candidate->parent->bone)
                {
                    candidate = candidate->parent;
                }
                if (!skeletonRoot)
                {
                    skeletonRoot = candidate;
                }
            }

            if (skeletonRoot)
            {
                auto rootIt = nodeToIndex.find(skeletonRoot);
                if (rootIt != nodeToIndex.end())
                {
                    gltfSkin.skeleton = rootIt->second;
                }
            }

            if (!inverseBindMatrices.empty())
            {
                gltfSkin.inverseBindMatrices = appendBufferViewAndAccessor(
                    model, builder, inverseBindMatrices,
                    TINYGLTF_COMPONENT_TYPE_FLOAT, TINYGLTF_TYPE_MAT4, 0);
            }

            model.skins.push_back(std::move(gltfSkin));
            skinToIndex.emplace(mesh, static_cast<int>(model.skins.size()) - 1);
        }
    }

    for (size_t i = 0; i < scene->nodes.count; ++i)
    {
        const ufbx_node* node = scene->nodes.data[i];
        if (!node || !node->mesh)
        {
            continue;
        }
        auto meshIt = meshToIndex.find(node->mesh);
        if (meshIt != meshToIndex.end())
        {
            model.nodes[i].mesh = meshIt->second;
        }
        auto skinIt = skinToIndex.find(node->mesh);
        if (skinIt != skinToIndex.end())
        {
            model.nodes[i].skin = skinIt->second;
        }
    }

    const std::vector<ExportClip> clips = loadSidecarClips(sourceFbxPath);
    for (const ExportClip& clip : clips)
    {
        tinygltf::Animation animation;
        animation.name = clip.name;

        auto addVec3Channels = [&](const std::unordered_map<std::string, ExportTrackVec3>& tracks,
                                   const char* path)
        {
            for (const auto& [boneName, track] : tracks)
            {
                auto nodeIt = nodeNameToIndex.find(boneName);
                if (nodeIt == nodeNameToIndex.end())
                {
                    continue;
                }

                const int inputAccessor = appendScalarAccessor(model, builder, track.times);
                const int outputAccessor = appendBufferViewAndAccessor(
                    model, builder, track.values,
                    TINYGLTF_COMPONENT_TYPE_FLOAT, TINYGLTF_TYPE_VEC3, 0);

                tinygltf::AnimationSampler sampler;
                sampler.input = inputAccessor;
                sampler.output = outputAccessor;
                sampler.interpolation = "LINEAR";
                animation.samplers.push_back(std::move(sampler));

                tinygltf::AnimationChannel channel;
                channel.sampler = static_cast<int>(animation.samplers.size()) - 1;
                channel.target_node = nodeIt->second;
                channel.target_path = path;
                animation.channels.push_back(std::move(channel));
            }
        };

        addVec3Channels(clip.translationTracks, "translation");
        addVec3Channels(clip.scaleTracks, "scale");

        for (const auto& [boneName, track] : clip.rotationTracks)
        {
            auto nodeIt = nodeNameToIndex.find(boneName);
            if (nodeIt == nodeNameToIndex.end())
            {
                continue;
            }

            const int inputAccessor = appendScalarAccessor(model, builder, track.times);
            const int outputAccessor = appendBufferViewAndAccessor(
                model, builder, track.values,
                TINYGLTF_COMPONENT_TYPE_FLOAT, TINYGLTF_TYPE_VEC4, 0);

            tinygltf::AnimationSampler sampler;
            sampler.input = inputAccessor;
            sampler.output = outputAccessor;
            sampler.interpolation = "LINEAR";
            animation.samplers.push_back(std::move(sampler));

            tinygltf::AnimationChannel channel;
            channel.sampler = static_cast<int>(animation.samplers.size()) - 1;
            channel.target_node = nodeIt->second;
            channel.target_path = "rotation";
            animation.channels.push_back(std::move(channel));
        }

        if (!animation.channels.empty())
        {
            model.animations.push_back(std::move(animation));
        }
    }

    model.buffers[0].data = std::move(builder.data);

    tinygltf::TinyGLTF writer;
    const std::string outputStd = outputPath.toStdString();
    const bool writeBinary = QFileInfo(outputPath).suffix().compare(QStringLiteral("glb"), Qt::CaseInsensitive) == 0;
    const bool ok = writer.WriteGltfSceneToFile(&model,
                                                outputStd,
                                                true,
                                                true,
                                                true,
                                                writeBinary);

    ufbx_free_scene(scene);

    if (!ok)
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("Failed to write glTF/GLB output.");
        }
        return false;
    }

    return true;
}

}
