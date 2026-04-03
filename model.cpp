#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include "model.h"
#include "engine.h"
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QByteArray>
#include <glm/glm.hpp>
#include <memory>
#include <numeric>
#include <utility>
#include <limits>
#include <cmath>
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/component_wise.hpp>
#include <cctype>
#include <cstring>
#include <iostream>
#include <filesystem>
#include <fstream>

#include "ufbx.h"
#include "stb_image.h"

#include <vector>

namespace
{
struct InstanceGpuData
{
    glm::vec4 offset = glm::vec4(0.0f);
    glm::vec4 rotation = glm::vec4(0.0f);
};

bool hasExtension(const std::string& path, const char* ext)
{
    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return std::filesystem::path(lower).extension() == ext;
}

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

glm::vec4 materialMapColor(const ufbx_material_map& map)
{
    switch (map.value_components)
    {
    case 4:
        return glm::vec4(static_cast<float>(map.value_vec4.x),
                         static_cast<float>(map.value_vec4.y),
                         static_cast<float>(map.value_vec4.z),
                         static_cast<float>(map.value_vec4.w));
    case 3:
        return glm::vec4(static_cast<float>(map.value_vec3.x),
                         static_cast<float>(map.value_vec3.y),
                         static_cast<float>(map.value_vec3.z),
                         1.0f);
    case 2:
        return glm::vec4(static_cast<float>(map.value_vec2.x),
                         static_cast<float>(map.value_vec2.y),
                         1.0f,
                         1.0f);
    case 1:
        return glm::vec4(static_cast<float>(map.value_real),
                         static_cast<float>(map.value_real),
                         static_cast<float>(map.value_real),
                         1.0f);
    default:
        return glm::vec4(1.0f);
    }
}

float materialMapScalar(const ufbx_material_map& map, float defaultValue = 1.0f)
{
    switch (map.value_components)
    {
    case 4:
        return static_cast<float>(map.value_vec4.x);
    case 3:
        return static_cast<float>(map.value_vec3.x);
    case 2:
        return static_cast<float>(map.value_vec2.x);
    case 1:
        return static_cast<float>(map.value_real);
    default:
        return defaultValue;
    }
}

float extractFbxMaterialAlpha(const ufbx_material* material)
{
    if (!material)
    {
        return 1.0f;
    }

    if (material->pbr.opacity.has_value)
    {
        return std::clamp(materialMapScalar(material->pbr.opacity, 1.0f), 0.0f, 1.0f);
    }

    if (material->fbx.transparency_factor.has_value)
    {
        const float transparency = std::clamp(materialMapScalar(material->fbx.transparency_factor, 0.0f), 0.0f, 1.0f);
        return 1.0f - transparency;
    }

    if (material->fbx.transparency_color.has_value)
    {
        const glm::vec4 transparency = glm::clamp(materialMapColor(material->fbx.transparency_color), glm::vec4(0.0f), glm::vec4(1.0f));
        return 1.0f - transparency.r;
    }

    return 1.0f;
}

const char* primitiveAlphaModeName(PrimitiveAlphaMode mode)
{
    switch (mode)
    {
    case PrimitiveAlphaMode::Opaque:
        return "opaque";
    case PrimitiveAlphaMode::Mask:
        return "mask";
    case PrimitiveAlphaMode::Blend:
        return "blend";
    }
    return "unknown";
}

const char* primitiveCullModeName(PrimitiveCullMode mode)
{
    switch (mode)
    {
    case PrimitiveCullMode::Back:
        return "back";
    case PrimitiveCullMode::Disabled:
        return "none";
    case PrimitiveCullMode::Front:
        return "front";
    }
    return "unknown";
}

PrimitiveAlphaMode classifyAlphaModeFromImage(const QImage& image,
                                              float uniformAlpha,
                                              bool hasOpacityImage)
{
    if (image.isNull())
    {
        if (uniformAlpha < 0.999f)
        {
            return PrimitiveAlphaMode::Blend;
        }
        return PrimitiveAlphaMode::Opaque;
    }

    int opaquePixels = 0;
    int transparentPixels = 0;
    int partialPixels = 0;
    const int pixelCount = image.width() * image.height();
    for (int y = 0; y < image.height(); ++y)
    {
        const uchar* scanline = image.constScanLine(y);
        for (int x = 0; x < image.width(); ++x)
        {
            const uchar alpha = scanline[x * 4 + 3];
            if (alpha >= 250)
            {
                ++opaquePixels;
            }
            else if (alpha <= 5)
            {
                ++transparentPixels;
            }
            else
            {
                ++partialPixels;
            }
        }
    }

    if (partialPixels == 0)
    {
        if (transparentPixels > 0 || hasOpacityImage)
        {
            return PrimitiveAlphaMode::Mask;
        }
        return PrimitiveAlphaMode::Opaque;
    }

    const float partialRatio = pixelCount > 0 ? static_cast<float>(partialPixels) / static_cast<float>(pixelCount) : 1.0f;
    const float transparentRatio = pixelCount > 0 ? static_cast<float>(transparentPixels) / static_cast<float>(pixelCount) : 0.0f;
    if (partialRatio <= 0.02f && transparentRatio >= 0.001f)
    {
        return PrimitiveAlphaMode::Mask;
    }

    return PrimitiveAlphaMode::Blend;
}

bool extractFbxMaterialColor(const ufbx_mesh* mesh, glm::vec4& outColor)
{
    if (!mesh || mesh->materials.count == 0 || !mesh->materials.data)
    {
        return false;
    }

    const ufbx_material* material = mesh->materials.data[0];
    if (!material)
    {
        return false;
    }

    if (material->pbr.base_color.has_value)
    {
        outColor = materialMapColor(material->pbr.base_color);
        if (material->pbr.base_factor.has_value)
        {
            const float factor = static_cast<float>(material->pbr.base_factor.value_real);
            outColor.r *= factor;
            outColor.g *= factor;
            outColor.b *= factor;
        }
        outColor.a = extractFbxMaterialAlpha(material);
        outColor = glm::clamp(outColor, glm::vec4(0.0f), glm::vec4(1.0f));
        return true;
    }

    if (material->fbx.diffuse_color.has_value)
    {
        outColor = materialMapColor(material->fbx.diffuse_color);
        if (material->fbx.diffuse_factor.has_value)
        {
            const float factor = static_cast<float>(material->fbx.diffuse_factor.value_real);
            outColor.r *= factor;
            outColor.g *= factor;
            outColor.b *= factor;
        }
        outColor.a = extractFbxMaterialAlpha(material);
        outColor = glm::clamp(outColor, glm::vec4(0.0f), glm::vec4(1.0f));
        return true;
    }

    return false;
}

QString ufbxStringToQString(ufbx_string value)
{
    return QString::fromUtf8(value.data, static_cast<qsizetype>(value.length));
}

int maxImportedTextureDimension()
{
    static int cached = []() {
        const char* env = std::getenv("MOTIVE_MAX_IMPORT_TEXTURE_DIM");
        if (!env || *env == '\0')
        {
            return 4096;
        }
        const int value = std::atoi(env);
        return value > 0 ? value : 4096;
    }();
    return cached;
}

void downscaleImportedImageIfNeeded(QImage& image, const QString& sourceLabel)
{
    if (image.isNull())
    {
        return;
    }
    const int maxDim = maxImportedTextureDimension();
    if (image.width() <= maxDim && image.height() <= maxDim)
    {
        return;
    }

    image = image.scaled(maxDim, maxDim, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    std::cout << "[Import] Downscaled oversized texture: "
              << sourceLabel.toStdString()
              << " -> " << image.width() << "x" << image.height()
              << " (maxDim=" << maxDim << ")" << std::endl;
}

bool loadImageFromUfbxTexture(const ufbx_texture* texture, const QString& sourceDirectory, QImage& outImage)
{
    if (!texture)
    {
        return false;
    }

    auto loadViaStbFromBytes = [&](const unsigned char* bytes, int size, const QString& sourceLabel) -> bool
    {
        if (!bytes || size <= 0)
        {
            return false;
        }
        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_uc* decoded = stbi_load_from_memory(bytes, size, &width, &height, &channels, STBI_rgb_alpha);
        if (!decoded)
        {
            return false;
        }

        QImage image(decoded, width, height, QImage::Format_RGBA8888);
        outImage = image.copy();
        stbi_image_free(decoded);
        if (!outImage.isNull())
        {
            downscaleImportedImageIfNeeded(outImage, sourceLabel);
            std::cout << "[FBX] Loaded texture via stb_image fallback: "
                      << sourceLabel.toStdString()
                      << " (" << width << "x" << height << ")" << std::endl;
            return true;
        }
        return false;
    };

    if (texture->content.data && texture->content.size > 0)
    {
        if (outImage.loadFromData(reinterpret_cast<const uchar*>(texture->content.data),
                                  static_cast<int>(texture->content.size)))
        {
            downscaleImportedImageIfNeeded(outImage, QStringLiteral("<embedded>"));
            return true;
        }
        if (loadViaStbFromBytes(reinterpret_cast<const unsigned char*>(texture->content.data),
                                static_cast<int>(texture->content.size),
                                QStringLiteral("<embedded>")))
        {
            return true;
        }
    }

    const QStringList candidates = {
        ufbxStringToQString(texture->filename),
        ufbxStringToQString(texture->absolute_filename),
        ufbxStringToQString(texture->relative_filename)
    };

    for (const QString& candidate : candidates)
    {
        if (candidate.isEmpty())
        {
            continue;
        }

        QString resolvedCandidate = candidate;
        const QFileInfo candidateInfo(candidate);
        if (candidateInfo.isRelative() && !sourceDirectory.isEmpty())
        {
            resolvedCandidate = QDir(sourceDirectory).filePath(candidate);
        }

        QImage image(resolvedCandidate);
        if (!image.isNull())
        {
            outImage = image;
            downscaleImportedImageIfNeeded(outImage, resolvedCandidate);
            return true;
        }

        std::ifstream file(resolvedCandidate.toStdString(), std::ios::binary);
        if (!file)
        {
            continue;
        }
        std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)),
                                         std::istreambuf_iterator<char>());
        if (bytes.empty())
        {
            continue;
        }
        if (loadViaStbFromBytes(bytes.data(),
                                static_cast<int>(bytes.size()),
                                resolvedCandidate))
        {
            return true;
        }
    }

    return false;
}

bool applyFbxBaseColorTexture(Mesh& targetMesh, const ufbx_mesh* sourceMesh)
{
    if (targetMesh.primitives.empty() || !sourceMesh || sourceMesh->materials.count == 0 || !sourceMesh->materials.data)
    {
        return false;
    }

    const ufbx_material* material = sourceMesh->materials.data[0];
    if (!material)
    {
        return false;
    }

    const ufbx_texture* texture = nullptr;
    const ufbx_material_map* sourceMap = nullptr;
    const ufbx_material_map* opacityMap = nullptr;
    bool invertOpacityMap = false;
    if (material->pbr.base_color.texture)
    {
        texture = material->pbr.base_color.texture;
        sourceMap = &material->pbr.base_color;
    }
    else if (material->fbx.diffuse_color.texture)
    {
        texture = material->fbx.diffuse_color.texture;
        sourceMap = &material->fbx.diffuse_color;
    }

    if (!texture || !sourceMap)
    {
        return false;
    }

    if (material->pbr.opacity.texture || material->pbr.opacity.has_value)
    {
        opacityMap = &material->pbr.opacity;
    }
    else if (material->fbx.transparency_factor.texture || material->fbx.transparency_factor.has_value)
    {
        opacityMap = &material->fbx.transparency_factor;
        invertOpacityMap = true;
    }

    const QString sourceDirectory = QFileInfo(QString::fromStdString(targetMesh.model->name)).absolutePath();
    QImage image;
    if (!loadImageFromUfbxTexture(texture, sourceDirectory, image) || image.isNull())
    {
        return false;
    }

    image = image.convertToFormat(QImage::Format_RGBA8888);
    if (image.isNull())
    {
        return false;
    }

    glm::vec4 factor = materialMapColor(*sourceMap);
    if (sourceMap == &material->pbr.base_color && material->pbr.base_factor.has_value)
    {
        const float scalar = static_cast<float>(material->pbr.base_factor.value_real);
        factor.r *= scalar;
        factor.g *= scalar;
        factor.b *= scalar;
    }
    if (sourceMap == &material->fbx.diffuse_color && material->fbx.diffuse_factor.has_value)
    {
        const float scalar = static_cast<float>(material->fbx.diffuse_factor.value_real);
        factor.r *= scalar;
        factor.g *= scalar;
        factor.b *= scalar;
    }
    factor = glm::clamp(factor, glm::vec4(0.0f), glm::vec4(1.0f));

    float opacityScalar = 1.0f;
    QImage opacityImage;
    if (opacityMap)
    {
        opacityScalar = std::clamp(materialMapScalar(*opacityMap, 1.0f), 0.0f, 1.0f);
        if (invertOpacityMap)
        {
            opacityScalar = 1.0f - opacityScalar;
        }
        if (opacityMap->texture && loadImageFromUfbxTexture(opacityMap->texture, sourceDirectory, opacityImage) && !opacityImage.isNull())
        {
            opacityImage = opacityImage.convertToFormat(QImage::Format_RGBA8888);
            if (opacityImage.size() != image.size())
            {
                opacityImage = opacityImage.scaled(image.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            }
        }
    }

    for (int y = 0; y < image.height(); ++y)
    {
        uchar* scanline = image.scanLine(y);
        const uchar* opacityScanline = opacityImage.isNull() ? nullptr : opacityImage.constScanLine(y);
        for (int x = 0; x < image.width(); ++x)
        {
            uchar* pixel = scanline + (x * 4);
            pixel[0] = static_cast<uchar>(std::clamp((pixel[0] / 255.0f) * factor.r, 0.0f, 1.0f) * 255.0f);
            pixel[1] = static_cast<uchar>(std::clamp((pixel[1] / 255.0f) * factor.g, 0.0f, 1.0f) * 255.0f);
            pixel[2] = static_cast<uchar>(std::clamp((pixel[2] / 255.0f) * factor.b, 0.0f, 1.0f) * 255.0f);
            float alpha = (pixel[3] / 255.0f) * factor.a * opacityScalar;
            if (opacityScanline)
            {
                const uchar* opacityPixel = opacityScanline + (x * 4);
                float opacitySample = opacityPixel[0] / 255.0f;
                if (invertOpacityMap)
                {
                    opacitySample = 1.0f - opacitySample;
                }
                alpha *= opacitySample;
            }
            pixel[3] = static_cast<uchar>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f);
        }
    }

    Primitive* primitive = targetMesh.primitives.front().get();
    if (primitive->primitiveDescriptorSet == VK_NULL_HANDLE)
    {
        primitive->createTextureFromPixelData(
            image.constBits(),
            static_cast<size_t>(image.sizeInBytes()),
            static_cast<uint32_t>(image.width()),
            static_cast<uint32_t>(image.height()),
            VK_FORMAT_R8G8B8A8_SRGB);
    }
    else
    {
        primitive->updateTextureFromPixelData(
            image.constBits(),
            static_cast<size_t>(image.sizeInBytes()),
            static_cast<uint32_t>(image.width()),
            static_cast<uint32_t>(image.height()),
            VK_FORMAT_R8G8B8A8_SRGB);
    }
    primitive->alphaMode = classifyAlphaModeFromImage(image, factor.a * opacityScalar, !opacityImage.isNull());
    primitive->alphaCutoff = 0.5f;
    std::cout << "[FBX] Material texture applied: mesh=\""
              << (sourceMesh && sourceMesh->name.data ? std::string(sourceMesh->name.data, sourceMesh->name.length) : std::string("<unnamed>"))
              << "\" size=" << image.width() << "x" << image.height()
              << " baseFactor=(" << factor.r << ", " << factor.g << ", " << factor.b << ", " << factor.a << ")"
              << " opacityScalar=" << opacityScalar
              << " opacityTexture=" << (!opacityImage.isNull() ? "yes" : "no")
              << " alphaMode=" << primitiveAlphaModeName(primitive->alphaMode)
              << " cullMode=" << primitiveCullModeName(primitive->cullMode)
              << std::endl;
    return true;
}

void applyFbxMaterialColorFallback(Mesh& targetMesh, const ufbx_mesh* sourceMesh)
{
    if (targetMesh.primitives.empty())
    {
        return;
    }

    glm::vec4 color(1.0f);
    if (!extractFbxMaterialColor(sourceMesh, color))
    {
        return;
    }

    const uint8_t rgba[4] = {
        static_cast<uint8_t>(std::clamp(color.r, 0.0f, 1.0f) * 255.0f),
        static_cast<uint8_t>(std::clamp(color.g, 0.0f, 1.0f) * 255.0f),
        static_cast<uint8_t>(std::clamp(color.b, 0.0f, 1.0f) * 255.0f),
        static_cast<uint8_t>(std::clamp(color.a, 0.0f, 1.0f) * 255.0f),
    };

    Primitive* primitive = targetMesh.primitives.front().get();
    if (primitive->primitiveDescriptorSet == VK_NULL_HANDLE)
    {
        primitive->createTextureFromPixelData(
            rgba,
            sizeof(rgba),
            1,
            1,
            VK_FORMAT_R8G8B8A8_SRGB);
    }
    else
    {
        primitive->updateTextureFromPixelData(
            rgba,
            sizeof(rgba),
            1,
            1,
            VK_FORMAT_R8G8B8A8_SRGB);
    }
    primitive->alphaMode = color.a < 0.999f ? PrimitiveAlphaMode::Blend : PrimitiveAlphaMode::Opaque;
    primitive->alphaCutoff = 0.5f;
    std::cout << "[FBX] Material color fallback: mesh=\""
              << (sourceMesh && sourceMesh->name.data ? std::string(sourceMesh->name.data, sourceMesh->name.length) : std::string("<unnamed>"))
              << "\" color=(" << color.r << ", " << color.g << ", " << color.b << ", " << color.a << ")"
              << " alphaMode=" << primitiveAlphaModeName(primitive->alphaMode)
              << " cullMode=" << primitiveCullModeName(primitive->cullMode)
              << std::endl;
}

std::vector<Vertex> buildVerticesFromFbxMesh(const ufbx_mesh* mesh)
{
    std::vector<Vertex> vertices;
    if (!mesh || !mesh->vertex_position.exists)
    {
        return vertices;
    }

    std::vector<uint32_t> triIndices(mesh->max_face_triangles * 3);
    vertices.reserve(mesh->num_triangles * 3);

    for (size_t faceIndex = 0; faceIndex < mesh->faces.count; ++faceIndex)
    {
        const ufbx_face face = mesh->faces.data[faceIndex];
        const uint32_t numTriangles = ufbx_triangulate_face(triIndices.data(), triIndices.size(), mesh, face);
        for (uint32_t tri = 0; tri < numTriangles * 3; ++tri)
        {
            const size_t index = triIndices[tri];
            Vertex vertex{};
            vertex.pos = toGlmVec3(ufbx_get_vertex_vec3(&mesh->vertex_position, index));
            vertex.normal = mesh->vertex_normal.exists
                ? toGlmVec3(ufbx_get_vertex_vec3(&mesh->vertex_normal, index))
                : glm::vec3(0.0f, 1.0f, 0.0f);
            vertex.texCoord = mesh->vertex_uv.exists
                ? toGlmVec2(ufbx_get_vertex_vec2(&mesh->vertex_uv, index))
                : glm::vec2(0.0f);
            vertices.push_back(vertex);
        }
    }

    return vertices;
}

bool extractVerticesAndIndicesFromGltfPrimitive(const tinygltf::Model& model,
                                                const tinygltf::Primitive& primitive,
                                                std::vector<Vertex>& outVertices,
                                                std::vector<uint32_t>& outIndices)
{
    if (primitive.attributes.count("POSITION") == 0 ||
        primitive.attributes.count("NORMAL") == 0 ||
        primitive.attributes.count("TEXCOORD_0") == 0)
    {
        std::cerr << "Skipping primitive due to missing attributes.\n";
        return false;
    }

    const auto& posAccessor = model.accessors[primitive.attributes.at("POSITION")];
    const auto& normAccessor = model.accessors[primitive.attributes.at("NORMAL")];
    const auto& texAccessor = model.accessors[primitive.attributes.at("TEXCOORD_0")];

    const auto& posView = model.bufferViews[posAccessor.bufferView];
    const auto& normView = model.bufferViews[normAccessor.bufferView];
    const auto& texView = model.bufferViews[texAccessor.bufferView];

    const float* positions = reinterpret_cast<const float*>(&model.buffers[posView.buffer].data[posView.byteOffset + posAccessor.byteOffset]);
    const float* normals = reinterpret_cast<const float*>(&model.buffers[normView.buffer].data[normView.byteOffset + normAccessor.byteOffset]);
    const float* texCoords = reinterpret_cast<const float*>(&model.buffers[texView.buffer].data[texView.byteOffset + texAccessor.byteOffset]);

    if (posAccessor.count != normAccessor.count || posAccessor.count != texAccessor.count)
    {
        throw std::runtime_error("GLTF attribute counts mismatch.");
    }

    outVertices.resize(posAccessor.count);
    for (size_t i = 0; i < posAccessor.count; ++i)
    {
        outVertices[i].pos = glm::vec3(positions[i * 3], positions[i * 3 + 1], positions[i * 3 + 2]);
        outVertices[i].normal = glm::vec3(normals[i * 3], normals[i * 3 + 1], normals[i * 3 + 2]);
        outVertices[i].texCoord = glm::vec2(texCoords[i * 2], texCoords[i * 2 + 1]);
    }

    if (primitive.indices >= 0)
    {
        const auto& indexAccessor = model.accessors[primitive.indices];
        const auto& indexView = model.bufferViews[indexAccessor.bufferView];
        const auto& indexBufferData = model.buffers[indexView.buffer];

        const uint8_t* dataPtr = reinterpret_cast<const uint8_t*>(
            &indexBufferData.data[indexView.byteOffset + indexAccessor.byteOffset]);

        auto componentSize = [](int componentType) -> size_t {
            switch (componentType)
            {
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                return sizeof(uint8_t);
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                return sizeof(uint16_t);
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                return sizeof(uint32_t);
            default:
                throw std::runtime_error("Unsupported index component type in GLTF");
            }
        };

        size_t stride = indexView.byteStride ? indexView.byteStride : componentSize(indexAccessor.componentType);
        outIndices.resize(indexAccessor.count);

        for (size_t i = 0; i < indexAccessor.count; ++i)
        {
            const uint8_t* elementPtr = dataPtr + i * stride;
            switch (indexAccessor.componentType)
            {
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                outIndices[i] = *reinterpret_cast<const uint8_t*>(elementPtr);
                break;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                outIndices[i] = *reinterpret_cast<const uint16_t*>(elementPtr);
                break;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                outIndices[i] = *reinterpret_cast<const uint32_t*>(elementPtr);
                break;
            default:
                throw std::runtime_error("Unsupported index component type in GLTF");
            }
        }
    }
    else
    {
        outIndices.resize(outVertices.size());
        std::iota(outIndices.begin(), outIndices.end(), 0u);
    }

    return true;
}
}

VkVertexInputBindingDescription Vertex::getBindingDescription()
{
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return bindingDescription;
}

std::array<VkVertexInputAttributeDescription, 3> Vertex::getAttributeDescriptions()
{
    std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};

    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(Vertex, pos);

    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(Vertex, normal);

    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].location = 2;
    attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[2].offset = offsetof(Vertex, texCoord);

    return attributeDescriptions;
}

Primitive::Primitive(Engine *engine, Mesh *mesh, const std::vector<Vertex> &vertices, bool initializeTextureResources)
    : engine(engine),
      mesh(mesh),
      vertexBuffer(VK_NULL_HANDLE),
      vertexBufferMemory(VK_NULL_HANDLE),
      vertexCount(static_cast<uint32_t>(vertices.size())),
      indexBuffer(VK_NULL_HANDLE),
      indexBufferMemory(VK_NULL_HANDLE),
      indexCount(0),
      transform(glm::mat4(1.0f)),
      rotation(glm::vec3(0.0f)),
      textureImage(VK_NULL_HANDLE),
      textureImageMemory(VK_NULL_HANDLE),
      textureImageView(VK_NULL_HANDLE),
      textureImageInactive(VK_NULL_HANDLE),
      textureImageMemoryInactive(VK_NULL_HANDLE),
      textureImageViewInactive(VK_NULL_HANDLE),
      textureSampler(VK_NULL_HANDLE),
      ObjectTransformUBO(VK_NULL_HANDLE),
      ObjectTransformUBOBufferMemory(VK_NULL_HANDLE),
      ObjectTransformUBOMapped(nullptr),
      primitiveDescriptorSet(VK_NULL_HANDLE)
{
    instanceOffsets.fill(glm::vec3(0.0f));
    instanceRotations.fill(glm::vec4(0.0f));

    // Create vertex buffer
    cpuVertices = vertices;
    VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;

    engine->createBuffer(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer,
        stagingBufferMemory);

    void *data;
    vkMapMemory(engine->logicalDevice, stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, vertices.data(), (size_t)bufferSize);
    vkUnmapMemory(engine->logicalDevice, stagingBufferMemory);

    engine->createBuffer(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        vertexBuffer,
        vertexBufferMemory);

    VkCommandBuffer cmdBuffer = engine->beginSingleTimeCommands();
    VkBufferCopy copyRegion{};
    copyRegion.size = bufferSize;
    vkCmdCopyBuffer(cmdBuffer, stagingBuffer, vertexBuffer, 1, &copyRegion);
    engine->endSingleTimeCommands(cmdBuffer);

    engine->deferStagingBufferDestruction(stagingBuffer, stagingBufferMemory);

    std::vector<uint32_t> sequentialIndices(vertexCount);
    std::iota(sequentialIndices.begin(), sequentialIndices.end(), 0);
    createIndexBuffer(sequentialIndices);

    // Create uniform buffer
    VkDeviceSize ObjectTransformUBOSize = sizeof(ObjectTransform);
    engine->createBuffer(
        ObjectTransformUBOSize,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        ObjectTransformUBO,
        ObjectTransformUBOBufferMemory);

    vkMapMemory(engine->logicalDevice, ObjectTransformUBOBufferMemory, 0, ObjectTransformUBOSize, 0, &ObjectTransformUBOMapped);

    instanceDataBufferSize = sizeof(InstanceGpuData) * kMaxPrimitiveInstances;
    engine->createBuffer(
        instanceDataBufferSize,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        instanceDataBuffer,
        instanceDataBufferMemory);
    vkMapMemory(engine->logicalDevice, instanceDataBufferMemory, 0, instanceDataBufferSize, 0, &instanceDataBufferMapped);
    std::memset(instanceDataBufferMapped, 0, static_cast<size_t>(instanceDataBufferSize));

    if (initializeTextureResources)
    {
        createTextureResources();
    }
}

Primitive::Primitive(Engine *engine,
                     Mesh *mesh,
                     const std::vector<Vertex> &vertices,
                     const std::vector<uint32_t> &indices,
                     int materialIndex)
    : engine(engine),
      mesh(mesh),
      vertexBuffer(VK_NULL_HANDLE),
      vertexBufferMemory(VK_NULL_HANDLE),
      vertexCount(static_cast<uint32_t>(vertices.size())),
      indexBuffer(VK_NULL_HANDLE),
      indexBufferMemory(VK_NULL_HANDLE),
      indexCount(0),
      transform(glm::mat4(1.0f)),
      rotation(glm::vec3(0.0f)),
      textureImage(VK_NULL_HANDLE),
      textureImageMemory(VK_NULL_HANDLE),
      textureImageView(VK_NULL_HANDLE),
      textureImageInactive(VK_NULL_HANDLE),
      textureImageMemoryInactive(VK_NULL_HANDLE),
      textureImageViewInactive(VK_NULL_HANDLE),
      textureSampler(VK_NULL_HANDLE),
      ObjectTransformUBO(VK_NULL_HANDLE),
      ObjectTransformUBOBufferMemory(VK_NULL_HANDLE),
      ObjectTransformUBOMapped(nullptr),
      primitiveDescriptorSet(VK_NULL_HANDLE)
{
    instanceOffsets.fill(glm::vec3(0.0f));
    instanceRotations.fill(glm::vec4(0.0f));

    cpuVertices = vertices;
    VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;

    engine->createBuffer(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer,
        stagingBufferMemory);

    void *data;
    vkMapMemory(engine->logicalDevice, stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, vertices.data(), static_cast<size_t>(bufferSize));
    vkUnmapMemory(engine->logicalDevice, stagingBufferMemory);

    engine->createBuffer(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        vertexBuffer,
        vertexBufferMemory);

    VkCommandBuffer cmdBuffer = engine->beginSingleTimeCommands();
    VkBufferCopy copyRegion{};
    copyRegion.size = bufferSize;
    vkCmdCopyBuffer(cmdBuffer, stagingBuffer, vertexBuffer, 1, &copyRegion);
    engine->endSingleTimeCommands(cmdBuffer);

    engine->deferStagingBufferDestruction(stagingBuffer, stagingBufferMemory);
    createIndexBuffer(indices);

    VkDeviceSize uboSize = sizeof(ObjectTransform);
    engine->createBuffer(
        uboSize,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        ObjectTransformUBO,
        ObjectTransformUBOBufferMemory);

    vkMapMemory(engine->logicalDevice, ObjectTransformUBOBufferMemory, 0, uboSize, 0, &ObjectTransformUBOMapped);

    instanceDataBufferSize = sizeof(InstanceGpuData) * kMaxPrimitiveInstances;
    engine->createBuffer(
        instanceDataBufferSize,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        instanceDataBuffer,
        instanceDataBufferMemory);
    vkMapMemory(engine->logicalDevice, instanceDataBufferMemory, 0, instanceDataBufferSize, 0, &instanceDataBufferMapped);
    std::memset(instanceDataBufferMapped, 0, static_cast<size_t>(instanceDataBufferSize));

    tinygltf::Primitive materialPrimitive;
    materialPrimitive.material = materialIndex;
    createTextureResources(mesh->model->tgltfModel, materialPrimitive);
}

Primitive::Primitive(Engine *engine, Mesh *mesh, tinygltf::Primitive tprimitive)
    : engine(engine),
      mesh(mesh),
      vertexBuffer(VK_NULL_HANDLE),
      vertexBufferMemory(VK_NULL_HANDLE),
      vertexCount(0),
      indexBuffer(VK_NULL_HANDLE),
      indexBufferMemory(VK_NULL_HANDLE),
      indexCount(0),
      transform(glm::mat4(1.0f)),
      rotation(glm::vec3(0.0f)),
      textureImage(VK_NULL_HANDLE),
      textureImageMemory(VK_NULL_HANDLE),
      textureImageView(VK_NULL_HANDLE),
      textureImageInactive(VK_NULL_HANDLE),
      textureImageMemoryInactive(VK_NULL_HANDLE),
      textureImageViewInactive(VK_NULL_HANDLE),
      textureSampler(VK_NULL_HANDLE),
      ObjectTransformUBO(VK_NULL_HANDLE),
      ObjectTransformUBOBufferMemory(VK_NULL_HANDLE),
      ObjectTransformUBOMapped(nullptr),
      primitiveDescriptorSet(VK_NULL_HANDLE)
{
    instanceOffsets.fill(glm::vec3(0.0f));
    instanceRotations.fill(glm::vec4(0.0f));
    Model *model = mesh->model;
    tinygltf::Model *tgltfModel = model->tgltfModel;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    if (!extractVerticesAndIndicesFromGltfPrimitive(*tgltfModel, tprimitive, vertices, indices))
    {
        return;
    }

    cpuVertices = vertices;
    vertexCount = static_cast<uint32_t>(vertices.size());

    // Create vertex buffer
    VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;

    engine->createBuffer(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer,
        stagingBufferMemory);

    void *data;
    vkMapMemory(engine->logicalDevice, stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, vertices.data(), (size_t)bufferSize);
    vkUnmapMemory(engine->logicalDevice, stagingBufferMemory);

    engine->createBuffer(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        vertexBuffer,
        vertexBufferMemory);

    VkCommandBuffer cmdBuffer = engine->beginSingleTimeCommands();
    VkBufferCopy copyRegion{};
    copyRegion.size = bufferSize;
    vkCmdCopyBuffer(cmdBuffer, stagingBuffer, vertexBuffer, 1, &copyRegion);
    engine->endSingleTimeCommands(cmdBuffer);

    engine->deferStagingBufferDestruction(stagingBuffer, stagingBufferMemory);

    createIndexBuffer(indices);

    // Create uniform buffer
    VkDeviceSize uboSize = sizeof(ObjectTransform);
    engine->createBuffer(
        uboSize,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        ObjectTransformUBO,
        ObjectTransformUBOBufferMemory);

    vkMapMemory(engine->logicalDevice, ObjectTransformUBOBufferMemory, 0, uboSize, 0, &ObjectTransformUBOMapped);

    instanceDataBufferSize = sizeof(InstanceGpuData) * kMaxPrimitiveInstances;
    engine->createBuffer(
        instanceDataBufferSize,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        instanceDataBuffer,
        instanceDataBufferMemory);
    vkMapMemory(engine->logicalDevice, instanceDataBufferMemory, 0, instanceDataBufferSize, 0, &instanceDataBufferMapped);
    std::memset(instanceDataBufferMapped, 0, static_cast<size_t>(instanceDataBufferSize));

    // Create texture resources using GLTF data when available
    createTextureResources(tgltfModel, tprimitive);
}

void Primitive::createIndexBuffer(const std::vector<uint32_t> &indices)
{
    if (indices.empty())
    {
        indexCount = 0;
        indexBuffer = VK_NULL_HANDLE;
        indexBufferMemory = VK_NULL_HANDLE;
        return;
    }

    indexCount = static_cast<uint32_t>(indices.size());
    VkDeviceSize bufferSize = sizeof(uint32_t) * indices.size();

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingBufferMemory = VK_NULL_HANDLE;

    engine->createBuffer(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer,
        stagingBufferMemory);

    void *data = nullptr;
    vkMapMemory(engine->logicalDevice, stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, indices.data(), static_cast<size_t>(bufferSize));
    vkUnmapMemory(engine->logicalDevice, stagingBufferMemory);

    engine->createBuffer(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        indexBuffer,
        indexBufferMemory);

    VkCommandBuffer cmdBuffer = engine->beginSingleTimeCommands();
    VkBufferCopy copyRegion{};
    copyRegion.size = bufferSize;
    vkCmdCopyBuffer(cmdBuffer, stagingBuffer, indexBuffer, 1, &copyRegion);
    engine->endSingleTimeCommands(cmdBuffer);

    engine->deferStagingBufferDestruction(stagingBuffer, stagingBufferMemory);
}

Primitive::~Primitive()
{
    const bool ownsSharedTexture = !sharedTextureResources;
    if (vertexBuffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(engine->logicalDevice, vertexBuffer, nullptr);
    }
    if (vertexBufferMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(engine->logicalDevice, vertexBufferMemory, nullptr);
    }
    if (ObjectTransformUBO != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(engine->logicalDevice, ObjectTransformUBO, nullptr);
    }
    if (ObjectTransformUBOBufferMemory != VK_NULL_HANDLE)
    {
        if (ObjectTransformUBOMapped)
        {
            vkUnmapMemory(engine->logicalDevice, ObjectTransformUBOBufferMemory);
        }
        vkFreeMemory(engine->logicalDevice, ObjectTransformUBOBufferMemory, nullptr);
    }
    if (instanceDataBuffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(engine->logicalDevice, instanceDataBuffer, nullptr);
    }
    if (instanceDataBufferMemory != VK_NULL_HANDLE)
    {
        if (instanceDataBufferMapped)
        {
            vkUnmapMemory(engine->logicalDevice, instanceDataBufferMemory);
        }
        vkFreeMemory(engine->logicalDevice, instanceDataBufferMemory, nullptr);
    }

    // Free descriptor set if allocated
    if (primitiveDescriptorSet != VK_NULL_HANDLE)
    {
        engine->freeDescriptorSet(engine->descriptorPool, primitiveDescriptorSet);
    }

    // Destroy texture resources
    if (ownsSharedTexture && textureImageView != VK_NULL_HANDLE)
    {
        vkDestroyImageView(engine->logicalDevice, textureImageView, nullptr);
    }
    if (ownsSharedTexture && textureImage != VK_NULL_HANDLE)
    {
        vkDestroyImage(engine->logicalDevice, textureImage, nullptr);
    }
    if (ownsSharedTexture && textureImageMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(engine->logicalDevice, textureImageMemory, nullptr);
    }
    if (textureImageViewInactive != VK_NULL_HANDLE)
    {
        vkDestroyImageView(engine->logicalDevice, textureImageViewInactive, nullptr);
    }
    if (textureImageInactive != VK_NULL_HANDLE)
    {
        vkDestroyImage(engine->logicalDevice, textureImageInactive, nullptr);
    }
    if (textureImageMemoryInactive != VK_NULL_HANDLE)
    {
        vkFreeMemory(engine->logicalDevice, textureImageMemoryInactive, nullptr);
    }

    if (chromaImageView != VK_NULL_HANDLE)
    {
        vkDestroyImageView(engine->logicalDevice, chromaImageView, nullptr);
    }
    if (chromaImage != VK_NULL_HANDLE)
    {
        vkDestroyImage(engine->logicalDevice, chromaImage, nullptr);
    }
    if (chromaImageMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(engine->logicalDevice, chromaImageMemory, nullptr);
    }
    if (chromaImageViewInactive != VK_NULL_HANDLE)
    {
        vkDestroyImageView(engine->logicalDevice, chromaImageViewInactive, nullptr);
    }
    if (chromaImageInactive != VK_NULL_HANDLE)
    {
        vkDestroyImage(engine->logicalDevice, chromaImageInactive, nullptr);
    }
    if (chromaImageMemoryInactive != VK_NULL_HANDLE)
    {
        vkFreeMemory(engine->logicalDevice, chromaImageMemoryInactive, nullptr);
    }

    // Destroy sampler
    if (ownsSharedTexture && textureSampler != VK_NULL_HANDLE)
    {
        vkDestroySampler(engine->logicalDevice, textureSampler, nullptr);
    }
    if (indexBuffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(engine->logicalDevice, indexBuffer, nullptr);
    }
    if (indexBufferMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(engine->logicalDevice, indexBufferMemory, nullptr);
    }
}

void Primitive::updateUniformBuffer(const glm::mat4 &model, const glm::mat4 &view, const glm::mat4 &proj)
{
    uploadInstanceTransforms();
    ObjectTransform thisTransformUBO = buildObjectTransformData();
    thisTransformUBO.model = model * transform;
    memcpy(ObjectTransformUBOMapped, &thisTransformUBO, sizeof(thisTransformUBO));
}

ObjectTransform Primitive::buildObjectTransformData() const
{
    ObjectTransform data{};
    const uint32_t clampedCount = std::max(1u, std::min(instanceCount, kMaxPrimitiveInstances));
    data.model = transform;
    const uint32_t formatValue = usesYuvTexture ? static_cast<uint32_t>(yuvTextureFormat) : 0u;
    data.instanceData = glm::uvec4(clampedCount,
                                   formatValue,
                                   yuvColorSpace,
                                   yuvColorRange);
    data.yuvParams = glm::uvec4(usesYuvTexture ? yuvChromaDivX : 1u,
                                usesYuvTexture ? yuvChromaDivY : 1u,
                                usesYuvTexture ? yuvBitDepth : 8u,
                                0u);
    data.materialFlags = glm::uvec4(static_cast<uint32_t>(alphaMode), paintOverrideEnabled ? 1u : 0u, 0u, 0u);
    data.materialParams = glm::vec4(alphaCutoff, 0.0f, 0.0f, 0.0f);
    data.colorOverride = glm::vec4(paintOverrideColor, 1.0f);
    return data;
}

void Primitive::markInstanceTransformsDirty()
{
    instanceTransformsDirty = true;
}

void Primitive::uploadInstanceTransforms()
{
    if (!instanceTransformsDirty || !instanceDataBufferMapped)
    {
        return;
    }

    auto *gpuData = reinterpret_cast<InstanceGpuData *>(instanceDataBufferMapped);
    for (uint32_t i = 0; i < kMaxPrimitiveInstances; ++i)
    {
        const bool active = i < instanceCount;
        const glm::vec3 offset = active ? instanceOffsets[i] : glm::vec3(0.0f);
        const glm::vec4 rotationVec = active ? instanceRotations[i] : glm::vec4(0.0f);
        gpuData[i].offset = glm::vec4(offset, 0.0f);
        gpuData[i].rotation = rotationVec;
    }
    instanceTransformsDirty = false;
}

void Primitive::updateDescriptorSet()
{
    if (primitiveDescriptorSet == VK_NULL_HANDLE)
    {
        throw std::runtime_error("Cannot update descriptor set: descriptor set is null.");
    }

    // Setup descriptor buffer info for UBO (binding 0)
    VkDescriptorBufferInfo thisObjectTransformBufferInfo{};
    thisObjectTransformBufferInfo.buffer = ObjectTransformUBO;
    thisObjectTransformBufferInfo.offset = 0;
    thisObjectTransformBufferInfo.range = sizeof(ObjectTransform);

    // Setup descriptor image info for texture sampler (binding 1)
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = textureImageView;
    imageInfo.sampler = textureSampler;

    VkDescriptorImageInfo chromaInfo = imageInfo;
    if (chromaImageView != VK_NULL_HANDLE)
    {
        chromaInfo.imageView = chromaImageView;
    }

    // Fill descriptor writes for UBO and sampler
    std::array<VkWriteDescriptorSet, 4> descriptorWrites{};

    // Binding 0: Uniform Buffer Object
    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = primitiveDescriptorSet;
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].dstArrayElement = 0;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pBufferInfo = &thisObjectTransformBufferInfo;

    // Binding 1: Combined Image Sampler
    descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[1].dstSet = primitiveDescriptorSet;
    descriptorWrites[1].dstBinding = 1;
    descriptorWrites[1].dstArrayElement = 0;
    descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[1].descriptorCount = 1;
    descriptorWrites[1].pImageInfo = &imageInfo;

    // Binding 2: Chroma sampler (falls back to base texture when unused)
    descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[2].dstSet = primitiveDescriptorSet;
    descriptorWrites[2].dstBinding = 2;
    descriptorWrites[2].dstArrayElement = 0;
    descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[2].descriptorCount = 1;
    descriptorWrites[2].pImageInfo = &chromaInfo;

    VkDescriptorBufferInfo instanceTransformBufferInfo{};
    instanceTransformBufferInfo.buffer = instanceDataBuffer;
    instanceTransformBufferInfo.offset = 0;
    instanceTransformBufferInfo.range = instanceDataBufferSize;

    descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[3].dstSet = primitiveDescriptorSet;
    descriptorWrites[3].dstBinding = 3;
    descriptorWrites[3].dstArrayElement = 0;
    descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[3].descriptorCount = 1;
    descriptorWrites[3].pBufferInfo = &instanceTransformBufferInfo;

    vkUpdateDescriptorSets(engine->logicalDevice,
                           static_cast<uint32_t>(descriptorWrites.size()),
                           descriptorWrites.data(),
                           0, nullptr);
}

void Primitive::setYuvColorMetadata(uint32_t colorSpace, uint32_t colorRange)
{
    yuvColorSpace = colorSpace;
    yuvColorRange = colorRange;
}

void Primitive::enableTextureDoubleBuffering()
{
    textureDoubleBuffered = true;
}


Mesh::Mesh(Engine *engine, Model *model, tinygltf::Mesh gltfmesh)
    : engine(engine), model(model)
{
    std::unordered_map<int, size_t> materialToGroupIndex;
    struct CombinedPrimitiveData
    {
        int materialIndex = -1;
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
    };
    std::vector<CombinedPrimitiveData> combinedGroups;

    for (const auto &tprimitive : gltfmesh.primitives)
    {
        std::vector<Vertex> primitiveVertices;
        std::vector<uint32_t> primitiveIndices;
        if (!extractVerticesAndIndicesFromGltfPrimitive(*model->tgltfModel, tprimitive, primitiveVertices, primitiveIndices))
        {
            continue;
        }

        const int materialIndex = tprimitive.material;
        auto it = materialToGroupIndex.find(materialIndex);
        if (it == materialToGroupIndex.end())
        {
            const size_t newIndex = combinedGroups.size();
            materialToGroupIndex.emplace(materialIndex, newIndex);
            combinedGroups.push_back(CombinedPrimitiveData{materialIndex, {}, {}});
            it = materialToGroupIndex.find(materialIndex);
        }

        CombinedPrimitiveData& group = combinedGroups[it->second];
        const uint32_t vertexOffset = static_cast<uint32_t>(group.vertices.size());
        group.vertices.insert(group.vertices.end(), primitiveVertices.begin(), primitiveVertices.end());
        group.indices.reserve(group.indices.size() + primitiveIndices.size());
        for (uint32_t index : primitiveIndices)
        {
            group.indices.push_back(vertexOffset + index);
        }
    }

    primitives.reserve(combinedGroups.size());
    for (auto& group : combinedGroups)
    {
        if (group.vertices.empty() || group.indices.empty())
        {
            continue;
        }
        primitives.emplace_back(std::make_unique<Primitive>(engine, this, group.vertices, group.indices, group.materialIndex));
    }
}

Mesh::Mesh(Engine *engine, Model *model, const std::vector<GltfCombinedPrimitiveData>& combinedPrimitives)
    : engine(engine), model(model)
{
    primitives.reserve(combinedPrimitives.size());
    for (const auto& group : combinedPrimitives)
    {
        if (group.vertices.empty() || group.indices.empty())
        {
            continue;
        }
        primitives.emplace_back(std::make_unique<Primitive>(engine, this, group.vertices, group.indices, group.materialIndex));
    }
}

Mesh::Mesh(Engine *engine, Model *model, const std::vector<Vertex> &vertices, bool initializeTextureResources)
    : engine(engine), model(model)
{
    primitives.emplace_back(std::make_unique<Primitive>(engine, this, vertices, initializeTextureResources));
}

Mesh::Mesh(Mesh &&other) noexcept
    : model(other.model),
      primitives(std::move(other.primitives)),
      engine(other.engine)
{
}

Mesh &Mesh::operator=(Mesh &&other) noexcept
{
    if (this != &other)
    {
        model = other.model;
        engine = other.engine;
        primitives = std::move(other.primitives);
    }
    return *this;
}

Mesh::~Mesh()
{
}

Model::Model(const std::vector<Vertex> &vertices, Engine *engine) : engine(engine)
{
    if (engine) engine->beginBatchUpload();
    std::cout << "[Debug] Creating procedural Model at " << this << " with " << vertices.size() << " vertices." << std::endl;
    // Create a single mesh from the provided vertices
    meshes.emplace_back(engine, this, vertices);
    normalizedBaseTransform = glm::mat4(1.0f);
    worldTransform = normalizedBaseTransform;
    std::cout << "[Debug] Model " << this << " finished initialization. Mesh count: " << meshes.size() << std::endl;
    recomputeBounds();
    if (engine) engine->endBatchUpload();
}

Model::Model(const std::string &gltfPath, Engine *engine, bool consolidateMeshes) : engine(engine)
{
    if (engine) engine->beginBatchUpload();
    name = gltfPath;
    meshConsolidationEnabled = consolidateMeshes;
    if (hasExtension(gltfPath, ".fbx"))
    {
        std::cout << "[Debug] Loading FBX Model at " << this << " from " << gltfPath << std::endl;
        ufbx_load_opts opts = {};
        opts.generate_missing_normals = true;
        ufbx_error error;
        ufbx_scene* scene = ufbx_load_file(gltfPath.c_str(), &opts, &error);
        if (!scene)
        {
            if (engine) engine->endBatchUpload();
            throw std::runtime_error("FBX error: " + std::string(error.description.data, error.description.length));
        }

        for (size_t i = 0; i < scene->anim_stacks.count; ++i)
        {
            const ufbx_anim_stack* animStack = scene->anim_stacks.data[i];
            if (!animStack)
            {
                continue;
            }
            std::string clipName = animStack->name.data && animStack->name.length > 0
                ? std::string(animStack->name.data, animStack->name.length)
                : ("Animation " + std::to_string(i));
            if (clipName.empty())
            {
                clipName = "Animation " + std::to_string(i);
            }
            animationClips.push_back(AnimationClipInfo{clipName});
        }
        std::cout << "[FBX] Animation stacks discovered: " << animationClips.size();
        if (!animationClips.empty())
        {
            std::cout << " [";
            for (size_t i = 0; i < animationClips.size(); ++i)
            {
                if (i > 0)
                {
                    std::cout << ", ";
                }
                std::cout << animationClips[i].name;
            }
            std::cout << "]";
        }
        std::cout << std::endl;

        for (size_t i = 0; i < scene->meshes.count; ++i)
        {
            const ufbx_mesh* mesh = scene->meshes.data[i];
            const std::vector<Vertex> vertices = buildVerticesFromFbxMesh(mesh);
            if (!vertices.empty())
            {
                meshes.emplace_back(engine, this, vertices, false);
                Primitive* primitive = meshes.back().primitives.empty() ? nullptr : meshes.back().primitives.front().get();
                if (!primitive)
                {
                    continue;
                }

                // FBX assets arriving from mixed DCC/export pipelines are not yet normalized for winding order,
                // so default to no culling in the editor/runtime path to avoid disappearing meshes.
                primitive->cullMode = PrimitiveCullMode::Disabled;
                primitive->createTextureSampler();
                const bool appliedTexture = applyFbxBaseColorTexture(meshes.back(), mesh);
                if (!appliedTexture)
                {
                    applyFbxMaterialColorFallback(meshes.back(), mesh);
                }
                if (primitive->textureImage == VK_NULL_HANDLE)
                {
                    primitive->createDefaultTexture();
                }
                primitive->finalizeTextureResources();
                std::cout << "[FBX] Mesh import summary: mesh=\""
                          << (mesh->name.data ? std::string(mesh->name.data, mesh->name.length) : std::string("<unnamed>"))
                          << "\" verts=" << primitive->vertexCount
                          << " indices=" << primitive->indexCount
                          << " textureApplied=" << (appliedTexture ? "yes" : "no")
                          << " alphaMode=" << primitiveAlphaModeName(primitive->alphaMode)
                          << " cullMode=" << primitiveCullModeName(primitive->cullMode)
                          << " alphaCutoff=" << primitive->alphaCutoff
                          << std::endl;
            }
        }

        ufbx_free_scene(scene);

        if (meshes.empty())
        {
            if (engine) engine->endBatchUpload();
            throw std::runtime_error("FBX contains no renderable meshes.");
        }

        std::cout << "[Debug] FBX Model " << this << " loaded successfully. Mesh count: " << meshes.size() << std::endl;
        recomputeBounds();
        if (engine) engine->endBatchUpload();
        return;
    }

    std::cout << "[Debug] Loading GLTF Model at " << this << " from " << gltfPath << std::endl;
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    bool success = false;
    const std::string ext = gltfPath.substr(gltfPath.find_last_of('.'));
    tgltfModel = new tinygltf::Model();

    if (ext == ".glb")
    {
        success = loader.LoadBinaryFromFile(tgltfModel, &err, &warn, gltfPath);
    }
    else if (ext == ".gltf")
    {
        success = loader.LoadASCIIFromFile(tgltfModel, &err, &warn, gltfPath);
    }
    else
    {
        if (engine) engine->endBatchUpload();
        throw std::runtime_error("Unsupported file extension: " + ext);
    }

    if (!warn.empty())
        std::cout << "GLTF warning: " << warn << std::endl;
    if (!err.empty())
    {
        if (engine) engine->endBatchUpload();
        throw std::runtime_error("GLTF error: " + err);
    }
    if (!success)
    {
        if (engine) engine->endBatchUpload();
        throw std::runtime_error("Failed to load GLTF file: " + gltfPath);
    }
    if (tgltfModel->meshes.empty())
    {
        if (engine) engine->endBatchUpload();
        throw std::runtime_error("GLTF contains no meshes.");
    }

    for (size_t i = 0; i < tgltfModel->animations.size(); ++i)
    {
        const auto& animation = tgltfModel->animations[i];
        std::string clipName = animation.name.empty() ? ("Animation " + std::to_string(i)) : animation.name;
        animationClips.push_back(AnimationClipInfo{clipName});
    }

    if (meshConsolidationEnabled)
    {
        std::unordered_map<int, size_t> materialToGroupIndex;
        std::vector<GltfCombinedPrimitiveData> combinedGroups;
        for (const auto &gltfmesh : tgltfModel->meshes)
        {
            for (const auto &tprimitive : gltfmesh.primitives)
            {
                std::vector<Vertex> primitiveVertices;
                std::vector<uint32_t> primitiveIndices;
                if (!extractVerticesAndIndicesFromGltfPrimitive(*tgltfModel, tprimitive, primitiveVertices, primitiveIndices))
                {
                    continue;
                }

                const int materialIndex = tprimitive.material;
                auto it = materialToGroupIndex.find(materialIndex);
                if (it == materialToGroupIndex.end())
                {
                    const size_t newIndex = combinedGroups.size();
                    materialToGroupIndex.emplace(materialIndex, newIndex);
                    combinedGroups.push_back(GltfCombinedPrimitiveData{materialIndex, {}, {}});
                    it = materialToGroupIndex.find(materialIndex);
                }

                GltfCombinedPrimitiveData& group = combinedGroups[it->second];
                const uint32_t vertexOffset = static_cast<uint32_t>(group.vertices.size());
                group.vertices.insert(group.vertices.end(), primitiveVertices.begin(), primitiveVertices.end());
                group.indices.reserve(group.indices.size() + primitiveIndices.size());
                for (uint32_t index : primitiveIndices)
                {
                    group.indices.push_back(vertexOffset + index);
                }
            }
        }

        meshes.clear();
        meshes.reserve(combinedGroups.empty() ? 0 : 1);
        if (!combinedGroups.empty())
        {
            meshes.emplace_back(engine, this, combinedGroups);
        }
    }
    else
    {
        meshes.reserve(tgltfModel->meshes.size());
        for (const auto &gltfmesh : tgltfModel->meshes)
        {
            meshes.emplace_back(engine, this, gltfmesh);
        }
    }

    std::cout << "[Debug] GLTF Model " << this << " loaded successfully. Mesh count: " << meshes.size() << std::endl;
    recomputeBounds();
    if (engine) engine->endBatchUpload();
}

Model::~Model()
{
    // Clear meshes vector - each Mesh's destructor will clean up its resources
    meshes.clear();

    // Clean up GLTF model storage if it was allocated
    if (tgltfModel != nullptr)
    {
        delete tgltfModel;
        tgltfModel = nullptr;
    }
}

void Model::scaleToUnitBox()
{
    glm::vec3 minBounds(std::numeric_limits<float>::max());
    glm::vec3 maxBounds(std::numeric_limits<float>::lowest());
    bool foundPositions = false;

    if (tgltfModel)
    {
        for (const auto &meshData : tgltfModel->meshes)
        {
            for (const auto &primitiveData : meshData.primitives)
            {
                auto attrIt = primitiveData.attributes.find("POSITION");
                if (attrIt == primitiveData.attributes.end())
                {
                    continue;
                }

                const tinygltf::Accessor &accessor = tgltfModel->accessors[attrIt->second];
                const tinygltf::BufferView &bufferView = tgltfModel->bufferViews[accessor.bufferView];
                const tinygltf::Buffer &buffer = tgltfModel->buffers[bufferView.buffer];

                size_t stride = accessor.ByteStride(bufferView);
                if (stride == 0)
                {
                    stride = 3 * sizeof(float);
                }

                const uint8_t *dataPtr = buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;
                for (size_t i = 0; i < accessor.count; ++i)
                {
                    const float *position = reinterpret_cast<const float *>(dataPtr + i * stride);
                    glm::vec3 pos(position[0], position[1], position[2]);
                    minBounds = glm::min(minBounds, pos);
                    maxBounds = glm::max(maxBounds, pos);
                    foundPositions = true;
                }
            }
        }
    }
    else
    {
        foundPositions = computeProceduralBounds(minBounds, maxBounds);
        if (!foundPositions)
        {
            std::cerr << "[Warning] scaleToUnitBox: Unable to compute bounds for procedural model " << name << std::endl;
            return;
        }
    }

    if (!foundPositions)
    {
        std::cerr << "[Warning] scaleToUnitBox: No position data found on model " << name << std::endl;
        return;
    }

    glm::vec3 extent = maxBounds - minBounds;
    float maxExtent = glm::compMax(extent);
    if (maxExtent <= 0.0f || !std::isfinite(maxExtent))
    {
        std::cerr << "[Warning] scaleToUnitBox: Invalid extent for model " << name << std::endl;
        return;
    }

    glm::vec3 center = (minBounds + maxBounds) * 0.5f;
    float scale = 1.0f / maxExtent;

    glm::mat4 translation = glm::translate(glm::mat4(1.0f), -center);
    glm::mat4 scaleMat = glm::scale(glm::mat4(1.0f), glm::vec3(scale));
    glm::mat4 transform = scaleMat * translation;
    normalizedBaseTransform = transform;
    worldTransform = transform;

    for (auto &mesh : meshes)
    {
        for (auto &primitive : mesh.primitives)
        {
                if (primitive)
                {
                    primitive->transform = transform;
                    if (primitive->ObjectTransformUBOMapped)
                    {
                        ObjectTransform updated = primitive->buildObjectTransformData();
                        memcpy(primitive->ObjectTransformUBOMapped, &updated, sizeof(updated));
                    }
                }
        }
    }

    recomputeBounds();
    std::cout << "[Debug] Model " << name << " scaled to unit box (scale=" << scale << ")" << std::endl;
}

void Model::resizeToUnitBox()
{
    // Currently identical to scaleToUnitBox but exposed with the requested name
    scaleToUnitBox();
}

void Model::scale(const glm::vec3 &factors)
{
    applyTransformToPrimitives(glm::scale(glm::mat4(1.0f), factors));
}

void Model::translate(const glm::vec3 &offset)
{
    applyTransformToPrimitives(glm::translate(glm::mat4(1.0f), offset));
}

void Model::rotate(float angleRadians, const glm::vec3 &axis)
{
    glm::vec3 normAxis = glm::length(axis) == 0.0f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::normalize(axis);
    applyTransformToPrimitives(glm::rotate(glm::mat4(1.0f), angleRadians, normAxis));
}

void Model::rotate(float xDegrees, float yDegrees, float zDegrees)
{
    glm::mat4 rotationMat(1.0f);

    if (xDegrees != 0.0f)
    {
        rotationMat = glm::rotate(rotationMat, glm::radians(xDegrees), glm::vec3(1.0f, 0.0f, 0.0f));
    }
    if (yDegrees != 0.0f)
    {
        rotationMat = glm::rotate(rotationMat, glm::radians(yDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
    }
    if (zDegrees != 0.0f)
    {
        rotationMat = glm::rotate(rotationMat, glm::radians(zDegrees), glm::vec3(0.0f, 0.0f, 1.0f));
    }

    applyTransformToPrimitives(rotationMat);
}

void Model::setSceneTransform(const glm::vec3& translation, const glm::vec3& rotationDegrees, const glm::vec3& scaleFactors)
{
    glm::mat4 modelMatrix = glm::translate(glm::mat4(1.0f), translation);
    modelMatrix = glm::rotate(modelMatrix, glm::radians(rotationDegrees.x), glm::vec3(1.0f, 0.0f, 0.0f));
    modelMatrix = glm::rotate(modelMatrix, glm::radians(rotationDegrees.y), glm::vec3(0.0f, 1.0f, 0.0f));
    modelMatrix = glm::rotate(modelMatrix, glm::radians(rotationDegrees.z), glm::vec3(0.0f, 0.0f, 1.0f));
    modelMatrix = glm::scale(modelMatrix, scaleFactors);
    modelMatrix = modelMatrix * normalizedBaseTransform;

    worldTransform = modelMatrix;
    for (auto& mesh : meshes)
    {
        for (auto& primitive : mesh.primitives)
        {
            if (!primitive)
            {
                continue;
            }
            primitive->transform = modelMatrix;
            if (primitive->ObjectTransformUBOMapped)
            {
                ObjectTransform updated = primitive->buildObjectTransformData();
                memcpy(primitive->ObjectTransformUBOMapped, &updated, sizeof(updated));
            }
        }
    }
    recomputeBounds();
}

void Model::setPaintOverride(bool enabled, const glm::vec3& color)
{
    for (auto& mesh : meshes)
    {
        for (auto& primitive : mesh.primitives)
        {
            if (!primitive)
            {
                continue;
            }
            primitive->paintOverrideEnabled = enabled;
            primitive->paintOverrideColor = color;
            if (primitive->ObjectTransformUBOMapped)
            {
                ObjectTransform updated = primitive->buildObjectTransformData();
                memcpy(primitive->ObjectTransformUBOMapped, &updated, sizeof(updated));
            }
        }
    }
}

void Model::applyTransformToPrimitives(const glm::mat4 &transform)
{
    worldTransform = transform * worldTransform;
    for (auto &mesh : meshes)
    {
        for (auto &primitive : mesh.primitives)
        {
            if (!primitive)
            {
                continue;
            }

            primitive->transform = transform * primitive->transform;

            if (primitive->ObjectTransformUBOMapped)
            {
                ObjectTransform updated = primitive->buildObjectTransformData();
                memcpy(primitive->ObjectTransformUBOMapped, &updated, sizeof(updated));
            }
        }
    }
    recomputeBounds();
}

void Model::recomputeBounds()
{
    glm::vec3 localMin(std::numeric_limits<float>::max());
    glm::vec3 localMax(std::numeric_limits<float>::lowest());
    bool found = false;
    for (const auto &mesh : meshes)
    {
        for (const auto &primitive : mesh.primitives)
        {
            if (!primitive)
                continue;
            for (const auto &vertex : primitive->cpuVertices)
            {
                localMin = glm::min(localMin, vertex.pos);
                localMax = glm::max(localMax, vertex.pos);
                found = true;
            }
        }
    }
    if (!found)
    {
        boundsCenter = glm::vec3(0.0f);
        boundsRadius = 0.0f;
        return;
    }
    glm::vec3 localCenter = (localMin + localMax) * 0.5f;
    float localRadius = glm::length(localMax - localMin) * 0.5f;

    boundsCenter = glm::vec3(worldTransform * glm::vec4(localCenter, 1.0f));

    glm::vec3 scale;
    scale.x = glm::length(glm::vec3(worldTransform[0]));
    scale.y = glm::length(glm::vec3(worldTransform[1]));
    scale.z = glm::length(glm::vec3(worldTransform[2]));
    boundsRadius = localRadius * glm::max(scale.x, glm::max(scale.y, scale.z));
}

bool Model::computeProceduralBounds(glm::vec3 &minBounds, glm::vec3 &maxBounds) const
{
    bool found = false;
    for (const auto &mesh : meshes)
    {
        for (const auto &primitive : mesh.primitives)
        {
            if (!primitive)
            {
                continue;
            }

            for (const auto &vertex : primitive->cpuVertices)
            {
                minBounds = glm::min(minBounds, vertex.pos);
                maxBounds = glm::max(maxBounds, vertex.pos);
                found = true;
            }
        }
    }
    return found;
}
