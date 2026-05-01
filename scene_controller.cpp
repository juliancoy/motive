#include "scene_controller.h"

#include "asset_loader.h"
#include "text_rendering.h"
#include "viewport_internal_utils.h"
#include "viewport_runtime.h"

#include "engine.h"
#include "model.h"
#include "primitive.h"

#include <QColor>
#include <QDebug>
#include <QFileInfo>
#include <algorithm>
#include <vulkan/vulkan.h>

namespace motive::ui {

namespace {
bool isTextOverlaySceneItem(const ViewportHostWidget::SceneItem& entry)
{
    return entry.sourcePath.startsWith(QStringLiteral("text://"), Qt::CaseInsensitive);
}

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

std::vector<Vertex> buildExtrudedTextVertices(const motive::text::OverlayBitmap& bitmap, float depth)
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

            // Front face keeps full UV mapping for crisp glyph sampling.
            appendTextFace(vertices, fbl, fbr, ftr, ftl, glm::vec3(0.0f, 0.0f, 1.0f), uva, uvb, uvc, uvd);

            // Back/side faces use texel-centered UVs to avoid stretched mirrored streaks.
            const glm::vec2 uvCenter((x0n + x1n) * 0.5f, (y0n + y1n) * 0.5f);
            appendTextFace(vertices, bbr, bbl, btl, btr, glm::vec3(0.0f, 0.0f, -1.0f), uvCenter, uvCenter, uvCenter, uvCenter);

            if (!opaque(x - 1, y))
            {
                appendTextFace(vertices, bbl, fbl, ftl, btl, glm::vec3(-1.0f, 0.0f, 0.0f), uvCenter, uvCenter, uvCenter, uvCenter);
            }
            if (!opaque(x + 1, y))
            {
                appendTextFace(vertices, fbr, bbr, btr, ftr, glm::vec3(1.0f, 0.0f, 0.0f), uvCenter, uvCenter, uvCenter, uvCenter);
            }
            if (!opaque(x, y - 1))
            {
                appendTextFace(vertices, ftl, ftr, btr, btl, glm::vec3(0.0f, 1.0f, 0.0f), uvCenter, uvCenter, uvCenter, uvCenter);
            }
            if (!opaque(x, y + 1))
            {
                appendTextFace(vertices, bbl, bbr, fbr, fbl, glm::vec3(0.0f, -1.0f, 0.0f), uvCenter, uvCenter, uvCenter, uvCenter);
            }
        }
    }
    return vertices;
}

uint32_t packedColorFromString(const QString& value, const QColor& fallback)
{
    QColor c(value);
    if (!c.isValid())
    {
        c = fallback;
    }
    return (static_cast<uint32_t>(c.alpha()) << 24u) |
           (static_cast<uint32_t>(c.red()) << 16u) |
           (static_cast<uint32_t>(c.green()) << 8u) |
           static_cast<uint32_t>(c.blue());
}

void applyTextSceneItemToModel(ViewportRuntime& runtime, int sceneIndex, const ViewportHostWidget::SceneItem& entry)
{
    if (!runtime.engine() || sceneIndex < 0)
    {
        return;
    }

    ViewportAssetLoader::ensureModelSlot(runtime, sceneIndex);
    if (sceneIndex >= static_cast<int>(runtime.engine()->models.size()))
    {
        return;
    }

    motive::text::FontRenderOptions fontOptions;
    fontOptions.fontPath = entry.textFontPath.toStdString();
    fontOptions.bold = entry.textBold;
    fontOptions.italic = entry.textItalic;
    fontOptions.letterSpacing = entry.textLetterSpacing;

    motive::text::TextOverlayStyle style;
    style.textColor = packedColorFromString(entry.textColor, QColor(Qt::white));
    style.backgroundColor = packedColorFromString(entry.textBackgroundColor, QColor(0, 0, 0, 170));
    style.drawShadow = entry.textShadow;
    style.drawOutline = entry.textOutline;
    style.drawBackground = true;

    motive::text::TextOverlayStyle meshStyle = style;
    if (entry.textExtrudeGlyphsOnly)
    {
        meshStyle.drawBackground = false;
        meshStyle.drawShadow = false;
    }

    const uint32_t pxHeight = static_cast<uint32_t>(std::max(8, entry.textPixelHeight));
    const size_t charCount = std::max<size_t>(entry.textContent.size(), 1u);
    const uint32_t overlayWidth = static_cast<uint32_t>(std::clamp<size_t>(charCount * static_cast<size_t>(pxHeight) * 2u, 1024u, 4096u));
    const uint32_t overlayHeight = static_cast<uint32_t>(std::clamp<uint32_t>(pxHeight * 4u, 256u, 1024u));

    const motive::text::OverlayBitmap textBitmap = motive::text::buildStyledTextOverlay(
        overlayWidth,
        overlayHeight,
        entry.textContent.toStdString(),
        pxHeight,
        fontOptions,
        style);
    const motive::text::OverlayBitmap meshBitmap = motive::text::buildStyledTextOverlay(
        overlayWidth,
        overlayHeight,
        entry.textContent.toStdString(),
        pxHeight,
        fontOptions,
        meshStyle);
    motive::text::OverlayBitmap glyphMeshBitmap = meshBitmap;
    if (entry.textExtrudeGlyphsOnly)
    {
        const motive::text::FontBitmap glyphBitmap = motive::text::renderText(
            entry.textContent.toStdString(),
            static_cast<uint32_t>(std::max(8, entry.textPixelHeight)),
            fontOptions);
        if (!glyphBitmap.pixels.empty() && glyphBitmap.width > 0 && glyphBitmap.height > 0)
        {
            glyphMeshBitmap.width = glyphBitmap.width;
            glyphMeshBitmap.height = glyphBitmap.height;
            glyphMeshBitmap.offsetX = 0;
            glyphMeshBitmap.offsetY = 0;
            glyphMeshBitmap.pixels = glyphBitmap.pixels;
        }
    }
    if (!textBitmap.pixels.empty() && textBitmap.width > 0 && textBitmap.height > 0)
    {
        const std::vector<Vertex> textVertices = buildExtrudedTextVertices(glyphMeshBitmap, std::max(0.0f, entry.textExtrudeDepth));
        if (textVertices.empty())
        {
            return;
        }
        if (!runtime.engine()->models[static_cast<size_t>(sceneIndex)])
        {
            auto model = std::make_unique<Model>(textVertices, runtime.engine());
            model->name = entry.name.toStdString();
            runtime.engine()->models[static_cast<size_t>(sceneIndex)] = std::move(model);
        }
        Model* model = runtime.engine()->models[static_cast<size_t>(sceneIndex)].get();
        if (!model || model->meshes.empty() || model->meshes.front().primitives.empty())
        {
            return;
        }
        Primitive* primitive = model->meshes.front().primitives.front().get();
        if (!primitive)
        {
            return;
        }
        primitive->updateVertexData(textVertices);
        primitive->updateTextureFromPixelData(
            textBitmap.pixels.data(),
            textBitmap.pixels.size(),
            textBitmap.width,
            textBitmap.height,
            VK_FORMAT_R8G8B8A8_UNORM);
        primitive->alphaMode = PrimitiveAlphaMode::Blend;
        primitive->cullMode = PrimitiveCullMode::Disabled;
        // Default behavior should favor legibility of text overlays in 3D scenes.
        primitive->depthTestEnabled = entry.textDepthTest;
        primitive->depthWriteEnabled = entry.textDepthWrite;

        model->setSceneTransform(glm::vec3(entry.translation.x(), entry.translation.y(), entry.translation.z()),
                                 glm::vec3(entry.rotation.x(), entry.rotation.y(), entry.rotation.z()),
                                 glm::vec3(entry.scale.x(), entry.scale.y(), entry.scale.z()));
        model->visible = entry.visible;
    }
}

void applyAnimationSettingsToModel(const ViewportHostWidget::SceneItem& entry, Model& model)
{
    model.setAnimationPlaybackState(entry.activeAnimationClip.toStdString(),
                                    entry.animationPlaying,
                                    entry.animationLoop,
                                    entry.animationSpeed);
    model.setAnimationProcessingOptions(entry.animationCentroidNormalization,
                                        entry.animationTrimStartNormalized,
                                        entry.animationTrimEndNormalized);
    model.setAnimationPhysicsCoupling(entry.animationPhysicsCoupling.toStdString());
    model.character.enableRestPointOnMoveRelease = entry.characterRestPointOnReleaseEnabled;
    model.character.restPointNormalizedOnMoveRelease =
        std::clamp(entry.characterRestPointOnReleaseNormalized, 0.0f, 1.0f);
}

}

ViewportSceneController::ViewportSceneController(ViewportRuntime& runtime)
    : m_runtime(runtime)
{
}

void ViewportSceneController::loadAssetFromPath(const QString& path)
{
    if (path.isEmpty())
    {
        return;
    }

    if (m_runtime.engine())
    {
        m_runtime.engine()->models.clear();
    }
    m_sceneEntries.clear();
    m_pendingSceneEntries.clear();
    m_pendingSceneEntries.push_back(ViewportHostWidget::SceneItem{
        QFileInfo(path).completeBaseName(),
        QFileInfo(path).absoluteFilePath(),
        m_meshConsolidationEnabled,
        QVector3D(0.0f, 0.0f, 0.0f),
        QVector3D(-90.0f, 0.0f, 0.0f),
        QVector3D(1.0f, 1.0f, 1.0f),
        false,
        QVector3D(1.0f, 0.0f, 1.0f),
        QString(),
        true,
        true,
        1.0f,
        true,
        0.0f,
        1.0f,
        true,
        QStringLiteral("AnimationOnly"),  // animationPhysicsCoupling
        true,  // useGravity
        QVector3D(0.0f, 0.0f, 0.0f)  // customGravity
    });
    m_currentAssetPath = path;
    if (!m_runtime.isInitialized() || !m_runtime.engine())
    {
        return;
    }
    addAssetToScene(path);
    m_pendingSceneEntries.clear();
}

void ViewportSceneController::loadSceneFromItems(const QList<ViewportHostWidget::SceneItem>& items)
{
    if (m_runtime.engine())
    {
        m_runtime.engine()->models.clear();
    }
    m_sceneEntries.clear();
    m_pendingSceneEntries.clear();

    for (const auto& item : items)
    {
        m_pendingSceneEntries.push_back(item);
    }
    m_currentAssetPath = items.isEmpty() ? QString() : items.back().sourcePath;

    if (!m_runtime.isInitialized() || !m_runtime.engine())
    {
        return;
    }

    restorePendingEntries();
}

void ViewportSceneController::addAssetToScene(const QString& path)
{
    if (path.isEmpty())
    {
        return;
    }

    m_currentAssetPath = path;
    if (!m_runtime.isInitialized() || !m_runtime.engine())
    {
        return;
    }

    try
    {
        ViewportHostWidget::SceneItem entry{
            QFileInfo(path).completeBaseName(),
            QFileInfo(path).absoluteFilePath(),
            m_meshConsolidationEnabled,
            QVector3D(static_cast<float>(m_runtime.engine()->models.size()) * 1.6f, 0.0f, 0.0f),
            QVector3D(-90.0f, 0.0f, 0.0f),
            QVector3D(1.0f, 1.0f, 1.0f),
            false,
            QVector3D(1.0f, 0.0f, 1.0f),
            QString(),
            true,
            true,
            1.0f,
            true,
            0.0f,
            1.0f,
            true
        };
        ViewportAssetLoader::loadModelIntoEngine(m_runtime, entry);
        if (!m_runtime.engine()->models.empty() && m_runtime.engine()->models.back() && entry.activeAnimationClip.isEmpty())
        {
            const auto& clips = m_runtime.engine()->models.back()->animationClips;
            if (!clips.empty())
            {
                entry.activeAnimationClip = QString::fromStdString(clips.front().name);
            }
        }
        if (!m_runtime.engine()->models.empty() && m_runtime.engine()->models.back())
        {
            applyAnimationSettingsToModel(entry, *m_runtime.engine()->models.back());
        }
        m_sceneEntries.push_back(entry);
    }
    catch (const std::exception& ex)
    {
        qWarning() << "[ViewportHost] Failed to add scene asset:" << path << ex.what();
    }
}

void ViewportSceneController::addTextOverlayItem()
{
    ViewportHostWidget::SceneItem entry;
    entry.name = QStringLiteral("Text");
    entry.sourcePath = QStringLiteral("text://overlay");
    entry.meshConsolidationEnabled = false;
    entry.translation = QVector3D(0.0f, 0.0f, 0.0f);
    entry.rotation = QVector3D(0.0f, 0.0f, 0.0f);
    entry.scale = QVector3D(1.0f, 1.0f, 1.0f);
    m_sceneEntries.push_back(entry);
    const int sceneIndex = m_sceneEntries.size() - 1;
    if (m_runtime.isInitialized() && m_runtime.engine())
    {
        applyTextSceneItemToModel(m_runtime, sceneIndex, m_sceneEntries[sceneIndex]);
    }
}

QString ViewportSceneController::currentAssetPath() const
{
    return m_currentAssetPath;
}

QList<ViewportHostWidget::SceneItem> ViewportSceneController::sceneItems() const
{
    QList<ViewportHostWidget::SceneItem> items = m_sceneEntries;
    for (const auto& entry : m_pendingSceneEntries)
    {
        items.push_back(entry);
    }
    return items;
}

bool ViewportSceneController::meshConsolidationEnabled() const
{
    return m_meshConsolidationEnabled;
}

void ViewportSceneController::setMeshConsolidationEnabled(bool enabled)
{
    if (m_meshConsolidationEnabled == enabled)
    {
        return;
    }

    m_meshConsolidationEnabled = enabled;
    const QList<ViewportHostWidget::SceneItem> items = sceneItems();

    if (m_runtime.engine())
    {
        m_runtime.engine()->models.clear();
    }
    m_sceneEntries.clear();
    m_pendingSceneEntries.clear();

    for (const auto& item : items)
    {
        m_pendingSceneEntries.push_back(item);
    }

    if (!m_runtime.isInitialized() || !m_runtime.engine())
    {
        return;
    }

    restorePendingEntries();
}

void ViewportSceneController::updateSceneItemTransform(int index, const QVector3D& translation, const QVector3D& rotation, const QVector3D& scale)
{
    if (index < 0 || index >= m_sceneEntries.size())
    {
        return;
    }

    m_sceneEntries[index].translation = translation;
    m_sceneEntries[index].rotation = rotation;
    m_sceneEntries[index].scale = scale;
    if (isTextOverlaySceneItem(m_sceneEntries[index]))
    {
        applyTextSceneItemToModel(m_runtime, index, m_sceneEntries[index]);
        return;
    }

    if (m_runtime.engine() &&
        index < static_cast<int>(m_runtime.engine()->models.size()) &&
        m_runtime.engine()->models[static_cast<size_t>(index)])
    {
        auto& model = m_runtime.engine()->models[static_cast<size_t>(index)];
        model->setSceneTransform(glm::vec3(translation.x(), translation.y(), translation.z()),
                                 glm::vec3(rotation.x(), rotation.y(), rotation.z()),
                                 glm::vec3(scale.x(), scale.y(), scale.z()));
        if (auto* body = model->getPhysicsBody())
        {
            body->syncTransformToPhysics();
        }
    }
}

void ViewportSceneController::setSceneItemMeshConsolidationEnabled(int index, bool enabled)
{
    if (index < 0 || index >= m_sceneEntries.size())
    {
        return;
    }
    if (m_sceneEntries[index].meshConsolidationEnabled == enabled)
    {
        return;
    }

    m_sceneEntries[index].meshConsolidationEnabled = enabled;
    if (!m_runtime.engine())
    {
        return;
    }

    ViewportAssetLoader::ensureModelSlot(m_runtime, index);
    if (index < static_cast<int>(m_runtime.engine()->models.size()) && m_runtime.engine()->models[static_cast<size_t>(index)])
    {
        if (m_runtime.engine()->logicalDevice != VK_NULL_HANDLE)
        {
            vkQueueWaitIdle(m_runtime.engine()->getGraphicsQueue());
            vkDeviceWaitIdle(m_runtime.engine()->logicalDevice);
        }
        m_runtime.engine()->models[static_cast<size_t>(index)].reset();
    }

    if (m_sceneEntries[index].visible)
    {
        try
        {
            ViewportAssetLoader::loadModelIntoEngineSlot(m_runtime, index, m_sceneEntries[index]);
            if (m_runtime.engine()->models[static_cast<size_t>(index)] && m_sceneEntries[index].activeAnimationClip.isEmpty())
            {
                const auto& clips = m_runtime.engine()->models[static_cast<size_t>(index)]->animationClips;
                if (!clips.empty())
                {
                    m_sceneEntries[index].activeAnimationClip = QString::fromStdString(clips.front().name);
                }
            }
            if (m_runtime.engine()->models[static_cast<size_t>(index)])
            {
                applyAnimationSettingsToModel(m_sceneEntries[index], *m_runtime.engine()->models[static_cast<size_t>(index)]);
            }
        }
        catch (const std::exception& ex)
        {
            qWarning() << "[ViewportHost] Failed to reload scene asset after load-parameter change:" << m_sceneEntries[index].sourcePath << ex.what();
        }
    }
}

void ViewportSceneController::updateSceneItemPaintOverride(int index, bool enabled, const QVector3D& color)
{
    if (index < 0 || index >= m_sceneEntries.size())
    {
        return;
    }

    m_sceneEntries[index].paintOverrideEnabled = enabled;
    m_sceneEntries[index].paintOverrideColor = color;

    if (m_runtime.engine() &&
        index < static_cast<int>(m_runtime.engine()->models.size()) &&
        m_runtime.engine()->models[static_cast<size_t>(index)])
    {
        m_runtime.engine()->models[static_cast<size_t>(index)]->setPaintOverride(enabled, glm::vec3(color.x(), color.y(), color.z()));
    }
}

void ViewportSceneController::updateSceneItemAnimationState(int index, const QString& activeClip, bool playing, bool loop, float speed)
{
    if (index < 0 || index >= m_sceneEntries.size())
    {
        return;
    }
    m_sceneEntries[index].activeAnimationClip = activeClip;
    m_sceneEntries[index].animationPlaying = playing;
    m_sceneEntries[index].animationLoop = loop;
    m_sceneEntries[index].animationSpeed = speed;

    if (m_runtime.engine() &&
        index < static_cast<int>(m_runtime.engine()->models.size()) &&
        m_runtime.engine()->models[static_cast<size_t>(index)])
    {
        m_runtime.engine()->models[static_cast<size_t>(index)]->setAnimationPlaybackState(activeClip.toStdString(), playing, loop, speed);
    }
}

void ViewportSceneController::updateSceneItemAnimationProcessing(int index,
                                                                 bool centroidNormalizationEnabled,
                                                                 float trimStartNormalized,
                                                                 float trimEndNormalized)
{
    if (index < 0 || index >= m_sceneEntries.size())
    {
        return;
    }

    const float trimStart = std::clamp(trimStartNormalized, 0.0f, 1.0f);
    const float trimEnd = std::clamp(trimEndNormalized, 0.0f, 1.0f);
    m_sceneEntries[index].animationCentroidNormalization = centroidNormalizationEnabled;
    m_sceneEntries[index].animationTrimStartNormalized = std::min(trimStart, trimEnd);
    m_sceneEntries[index].animationTrimEndNormalized = std::max(trimStart, trimEnd);

    if (m_runtime.engine() &&
        index < static_cast<int>(m_runtime.engine()->models.size()) &&
        m_runtime.engine()->models[static_cast<size_t>(index)])
    {
        m_runtime.engine()->models[static_cast<size_t>(index)]->setAnimationProcessingOptions(
            m_sceneEntries[index].animationCentroidNormalization,
            m_sceneEntries[index].animationTrimStartNormalized,
            m_sceneEntries[index].animationTrimEndNormalized);
    }
}

void ViewportSceneController::updateSceneItemAnimationPhysicsCoupling(int index, const QString& couplingMode)
{
    if (index < 0 || index >= m_sceneEntries.size())
    {
        return;
    }
    m_sceneEntries[index].animationPhysicsCoupling = couplingMode;

    if (m_runtime.engine() &&
        index < static_cast<int>(m_runtime.engine()->models.size()) &&
        m_runtime.engine()->models[static_cast<size_t>(index)])
    {
        m_runtime.engine()->models[static_cast<size_t>(index)]->setAnimationPhysicsCoupling(couplingMode.toStdString());
    }
}

void ViewportSceneController::updateSceneItemPhysicsGravity(int index, bool useGravity, const QVector3D& customGravity)
{
    if (index < 0 || index >= m_sceneEntries.size())
    {
        return;
    }
    m_sceneEntries[index].useGravity = useGravity;
    m_sceneEntries[index].customGravity = customGravity;

    if (m_runtime.engine() &&
        index < static_cast<int>(m_runtime.engine()->models.size()) &&
        m_runtime.engine()->models[static_cast<size_t>(index)])
    {
        auto& model = m_runtime.engine()->models[static_cast<size_t>(index)];
        model->setUseGravity(useGravity);
        model->setCustomGravity(glm::vec3(customGravity.x(), customGravity.y(), customGravity.z()));
        
        // If physics body already exists, update it
        if (auto* body = model->getPhysicsBody())
        {
            body->setUseGravity(useGravity);
            if (customGravity.x() != 0.0f || customGravity.y() != 0.0f || customGravity.z() != 0.0f)
            {
                body->setCustomGravity(glm::vec3(customGravity.x(), customGravity.y(), customGravity.z()));
            }
        }
    }
}

void ViewportSceneController::updateSceneItemCharacterTurnResponsiveness(int index, float responsiveness)
{
    if (index < 0 || index >= m_sceneEntries.size())
    {
        return;
    }

    const float clamped = (responsiveness > 0.01f) ? responsiveness : 0.01f;
    m_sceneEntries[index].characterTurnResponsiveness = clamped;

    if (m_runtime.engine() &&
        index < static_cast<int>(m_runtime.engine()->models.size()) &&
        m_runtime.engine()->models[static_cast<size_t>(index)])
    {
        m_runtime.engine()->models[static_cast<size_t>(index)]->character.turnResponsiveness = clamped;
    }
}

void ViewportSceneController::updateSceneItemCharacterRestPointOnRelease(int index, bool enabled, float normalized)
{
    if (index < 0 || index >= m_sceneEntries.size())
    {
        return;
    }

    const float clamped = std::clamp(normalized, 0.0f, 1.0f);
    m_sceneEntries[index].characterRestPointOnReleaseEnabled = enabled;
    m_sceneEntries[index].characterRestPointOnReleaseNormalized = clamped;

    if (m_runtime.engine() &&
        index < static_cast<int>(m_runtime.engine()->models.size()) &&
        m_runtime.engine()->models[static_cast<size_t>(index)])
    {
        auto& character = m_runtime.engine()->models[static_cast<size_t>(index)]->character;
        character.enableRestPointOnMoveRelease = enabled;
        character.restPointNormalizedOnMoveRelease = clamped;
    }
}

void ViewportSceneController::updateSceneItemFocusSettings(int index, const QVector3D& focusPointOffset, float focusDistance)
{
    if (index < 0 || index >= m_sceneEntries.size())
    {
        return;
    }

    m_sceneEntries[index].focusPointOffset = focusPointOffset;
    m_sceneEntries[index].focusDistance = focusDistance > 0.0f ? focusDistance : 0.0f;
}

void ViewportSceneController::updateSceneItemFocusCameraOffset(int index, const QVector3D& focusCameraOffset, bool valid)
{
    if (index < 0 || index >= m_sceneEntries.size())
    {
        return;
    }

    m_sceneEntries[index].focusCameraOffset = focusCameraOffset;
    m_sceneEntries[index].focusCameraOffsetValid = valid;
}

void ViewportSceneController::renameSceneItem(int index, const QString& name)
{
    if (index < 0 || index >= m_sceneEntries.size())
    {
        return;
    }

    const QString trimmed = name.trimmed();
    if (!trimmed.isEmpty())
    {
        m_sceneEntries[index].name = trimmed;
    }
}

void ViewportSceneController::setSceneItemVisible(int index, bool visible)
{
    if (index < 0 || index >= m_sceneEntries.size())
    {
        return;
    }

    m_sceneEntries[index].visible = visible;
    if (isTextOverlaySceneItem(m_sceneEntries[index]))
    {
        applyTextSceneItemToModel(m_runtime, index, m_sceneEntries[index]);
        return;
    }
    if (!m_runtime.engine())
    {
        return;
    }

    ViewportAssetLoader::ensureModelSlot(m_runtime, index);
    if (visible && !m_runtime.engine()->models[static_cast<size_t>(index)])
    {
        try
        {
            ViewportAssetLoader::loadModelIntoEngineSlot(m_runtime, index, m_sceneEntries[index]);
            if (m_runtime.engine()->models[static_cast<size_t>(index)] && m_sceneEntries[index].activeAnimationClip.isEmpty())
            {
                const auto& clips = m_runtime.engine()->models[static_cast<size_t>(index)]->animationClips;
                if (!clips.empty())
                {
                    m_sceneEntries[index].activeAnimationClip = QString::fromStdString(clips.front().name);
                }
            }
            if (m_runtime.engine()->models[static_cast<size_t>(index)])
            {
                applyAnimationSettingsToModel(m_sceneEntries[index], *m_runtime.engine()->models[static_cast<size_t>(index)]);
            }
        }
        catch (const std::exception& ex)
        {
            qWarning() << "[ViewportHost] Failed to lazy-load hidden scene asset:" << m_sceneEntries[index].sourcePath << ex.what();
            m_sceneEntries[index].visible = false;
        }
    }
    if (index < static_cast<int>(m_runtime.engine()->models.size()) && m_runtime.engine()->models[static_cast<size_t>(index)])
    {
        m_runtime.engine()->models[static_cast<size_t>(index)]->visible = m_sceneEntries[index].visible;
    }
}

void ViewportSceneController::updateSceneItemTextOverlay(int index, const ViewportHostWidget::SceneItem& textProps)
{
    if (index < 0 || index >= m_sceneEntries.size())
    {
        return;
    }
    if (!isTextOverlaySceneItem(m_sceneEntries[index]))
    {
        return;
    }

    m_sceneEntries[index].textContent = textProps.textContent;
    m_sceneEntries[index].textFontPath = textProps.textFontPath;
    m_sceneEntries[index].textPixelHeight = textProps.textPixelHeight;
    m_sceneEntries[index].textBold = textProps.textBold;
    m_sceneEntries[index].textItalic = textProps.textItalic;
    m_sceneEntries[index].textShadow = textProps.textShadow;
    m_sceneEntries[index].textOutline = textProps.textOutline;
    m_sceneEntries[index].textLetterSpacing = textProps.textLetterSpacing;
    m_sceneEntries[index].textColor = textProps.textColor;
    m_sceneEntries[index].textBackgroundColor = textProps.textBackgroundColor;
    m_sceneEntries[index].textExtrudeDepth = textProps.textExtrudeDepth;
    m_sceneEntries[index].textExtrudeGlyphsOnly = textProps.textExtrudeGlyphsOnly;
    m_sceneEntries[index].textDepthTest = textProps.textDepthTest;
    m_sceneEntries[index].textDepthWrite = textProps.textDepthWrite;
    applyTextSceneItemToModel(m_runtime, index, m_sceneEntries[index]);
}

void ViewportSceneController::deleteSceneItem(int index)
{
    if (index < 0 || index >= m_sceneEntries.size())
    {
        return;
    }

    m_sceneEntries.removeAt(index);
    if (m_runtime.engine() && index < static_cast<int>(m_runtime.engine()->models.size()))
    {
        if (m_runtime.engine()->logicalDevice != VK_NULL_HANDLE)
        {
            vkQueueWaitIdle(m_runtime.engine()->getGraphicsQueue());
            vkDeviceWaitIdle(m_runtime.engine()->logicalDevice);
        }
        m_runtime.engine()->models.erase(m_runtime.engine()->models.begin() + index);
    }

    if (m_sceneEntries.isEmpty())
    {
        m_currentAssetPath.clear();
    }
    else if (index - 1 >= 0)
    {
        m_currentAssetPath = m_sceneEntries[index - 1].sourcePath;
    }
    else
    {
        m_currentAssetPath = m_sceneEntries.front().sourcePath;
    }
}

QList<ViewportHostWidget::SceneItem>& ViewportSceneController::loadedEntries()
{
    return m_sceneEntries;
}

const QList<ViewportHostWidget::SceneItem>& ViewportSceneController::loadedEntries() const
{
    return m_sceneEntries;
}

QList<ViewportHostWidget::SceneItem>& ViewportSceneController::pendingEntries()
{
    return m_pendingSceneEntries;
}

const QList<ViewportHostWidget::SceneItem>& ViewportSceneController::pendingEntries() const
{
    return m_pendingSceneEntries;
}

void ViewportSceneController::restorePendingEntries()
{
    const QList<ViewportHostWidget::SceneItem> pendingEntries = m_pendingSceneEntries;
    m_pendingSceneEntries.clear();

    // Track total loading time
    auto totalStart = std::chrono::high_resolution_clock::now();
    
    // Use batch loading for multiple entries when parallel loading is enabled
    // TEMPORARILY DISABLED - testing crash fix
    if (false && pendingEntries.size() > 1 && 
        m_runtime.engine() && 
        m_runtime.engine()->isParallelModelLoadingEnabled())
    {
        std::vector<std::pair<int, ViewportHostWidget::SceneItem>> items;
        items.reserve(pendingEntries.size());
        
        int startIndex = m_sceneEntries.size();
        for (int i = 0; i < pendingEntries.size(); ++i)
        {
            items.push_back({startIndex + i, pendingEntries[i]});
        }
        
        qDebug() << "[ViewportSceneController] Batch loading" << items.size() << "models with parallel loader";
        
        // Load all models in parallel
        ViewportAssetLoader::loadModelsIntoEngineBatch(m_runtime, items,
            [](int completed, int total) {
                qDebug() << "[ViewportSceneController] Loading progress:" << completed << "/" << total;
            });
        
        // Update scene entries with animation clip info
        for (int i = 0; i < pendingEntries.size(); ++i)
        {
            auto entry = pendingEntries[i];
            const int sceneIndex = startIndex + i;
            
            if (m_runtime.engine() &&
                sceneIndex < static_cast<int>(m_runtime.engine()->models.size()) &&
                m_runtime.engine()->models[static_cast<size_t>(sceneIndex)] &&
                entry.activeAnimationClip.isEmpty())
            {
                const auto& clips = m_runtime.engine()->models[static_cast<size_t>(sceneIndex)]->animationClips;
                if (!clips.empty())
                {
                    entry.activeAnimationClip = QString::fromStdString(clips.front().name);
                }
            }
            m_sceneEntries.push_back(entry);
        }
        
        auto totalEnd = std::chrono::high_resolution_clock::now();
        auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(totalEnd - totalStart).count();
        qDebug() << "[ViewportSceneController] PARALLEL load complete:" << items.size() << "models in" << totalMs << "ms";
    }
    else
    {
        // Sequential loading (single item or parallel disabled)
        qDebug() << "[ViewportSceneController] Sequential loading" << pendingEntries.size() << "models";
        
        for (auto entry : pendingEntries)
        {
            auto modelStart = std::chrono::high_resolution_clock::now();
            
            try
            {
                const int sceneIndex = m_sceneEntries.size();
                if (isTextOverlaySceneItem(entry))
                {
                    applyTextSceneItemToModel(m_runtime, sceneIndex, entry);
                    m_sceneEntries.push_back(entry);
                    continue;
                }
                if (entry.visible)
                {
                    ViewportAssetLoader::loadModelIntoEngineSlot(m_runtime, sceneIndex, entry);
                }
                else
                {
                    ViewportAssetLoader::ensureModelSlot(m_runtime, sceneIndex);
                }

                auto modelEnd = std::chrono::high_resolution_clock::now();
                auto modelMs = std::chrono::duration_cast<std::chrono::milliseconds>(modelEnd - modelStart).count();
                qDebug() << "[ViewportSceneController] Sequential load:" << entry.name << "took" << modelMs << "ms";

                if (m_runtime.engine() &&
                    sceneIndex < static_cast<int>(m_runtime.engine()->models.size()) &&
                    m_runtime.engine()->models[static_cast<size_t>(sceneIndex)] &&
                    entry.activeAnimationClip.isEmpty())
                {
                    const auto& clips = m_runtime.engine()->models[static_cast<size_t>(sceneIndex)]->animationClips;
                    if (!clips.empty())
                    {
                        entry.activeAnimationClip = QString::fromStdString(clips.front().name);
                    }
                }
                if (m_runtime.engine() &&
                    sceneIndex < static_cast<int>(m_runtime.engine()->models.size()) &&
                    m_runtime.engine()->models[static_cast<size_t>(sceneIndex)])
                {
                    applyAnimationSettingsToModel(entry, *m_runtime.engine()->models[static_cast<size_t>(sceneIndex)]);
                }

                m_sceneEntries.push_back(entry);
            }
            catch (const std::exception& ex)
            {
                qWarning() << "[ViewportHost] Failed to restore scene asset:" << entry.sourcePath << ex.what();
            }
        }
        
        auto totalEnd = std::chrono::high_resolution_clock::now();
        auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(totalEnd - totalStart).count();
        qDebug() << "[ViewportSceneController] SEQUENTIAL load complete:" << pendingEntries.size() << "models in" << totalMs << "ms";
    }
}

}  // namespace motive::ui
