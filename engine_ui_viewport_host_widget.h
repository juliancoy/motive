#pragma once

#include <QColor>
#include <QImage>
#include <QJsonArray>
#include <QTimer>
#include <QVector3D>
#include <QWidget>
#include <QString>
#include <QStringList>
#include <functional>

class QLabel;
class QDragEnterEvent;
class QDropEvent;

namespace motive::ui {

class ViewportHostWidget : public QWidget
{
public:
    struct SceneItem
    {
        QString name;
        QString sourcePath;
        bool meshConsolidationEnabled = true;
        QVector3D translation;
        QVector3D rotation;
        QVector3D scale;
        bool paintOverrideEnabled = false;
        QVector3D paintOverrideColor = QVector3D(1.0f, 0.0f, 1.0f);
        QString activeAnimationClip;
        bool animationPlaying = true;
        bool animationLoop = true;
        float animationSpeed = 1.0f;
        bool visible = true;
    };

    struct SceneLight
    {
        QString type = QStringLiteral("directional");
        bool exists = false;
        QVector3D direction = QVector3D(0.0f, 0.0f, 1.0f);
        QVector3D color = QVector3D(1.0f, 1.0f, 1.0f);
        float brightness = 1.0f;
        QVector3D ambient = QVector3D(0.1f, 0.1f, 0.1f);
        QVector3D diffuse = QVector3D(0.9f, 0.9f, 0.9f);
    };

    struct HierarchyNode
    {
        enum class Type
        {
            Camera,
            Light,
            SceneItem,
            Mesh,
            Primitive,
            Material,
            Texture,
            AnimationGroup,
            AnimationClip,
            PendingSceneItem
        };

        QString label;
        Type type = Type::SceneItem;
        int sceneIndex = -1;
        int meshIndex = -1;
        int primitiveIndex = -1;
        QString clipName;
        QList<HierarchyNode> children;
    };

    explicit ViewportHostWidget(QWidget* parent = nullptr);
    ~ViewportHostWidget() override;

    void loadAssetFromPath(const QString& path);
    void loadSceneFromItems(const QList<SceneItem>& items);
    void addAssetToScene(const QString& path);
    QString currentAssetPath() const;
    QList<SceneItem> sceneItems() const;
    QList<HierarchyNode> hierarchyItems() const;
    QJsonArray hierarchyJson() const;
    QJsonArray sceneProfileJson() const;
    QImage primitiveTexturePreview(int sceneIndex, int meshIndex, int primitiveIndex) const;
    QString animationExecutionMode(int sceneIndex, int meshIndex = -1, int primitiveIndex = -1) const;
    QString primitiveCullMode(int sceneIndex, int meshIndex, int primitiveIndex) const;
    bool primitiveForceAlphaOne(int sceneIndex, int meshIndex, int primitiveIndex) const;
    QStringList animationClipNames(int sceneIndex) const;
    QVector3D cameraPosition() const;
    QVector3D cameraRotation() const;
    float cameraSpeed() const;
    QString renderPath() const;
    bool meshConsolidationEnabled() const;
    bool hasSceneLight() const;
    SceneLight sceneLight() const;
    void setCameraPosition(const QVector3D& position);
    void setCameraRotation(const QVector3D& rotation);
    void setCameraSpeed(float speed);
    void resetCamera();
    void setBackgroundColor(const QColor& color);
    void setRenderPath(const QString& renderPath);
    void setMeshConsolidationEnabled(bool enabled);
    void createSceneLight();
    void setSceneLight(const SceneLight& light);
    void updateSceneItemTransform(int index, const QVector3D& translation, const QVector3D& rotation, const QVector3D& scale);
    void setSceneItemMeshConsolidationEnabled(int index, bool enabled);
    void updateSceneItemPaintOverride(int index, bool enabled, const QVector3D& color);
    void updateSceneItemAnimationState(int index, const QString& activeClip, bool playing, bool loop, float speed);
    void renameSceneItem(int index, const QString& name);
    void setSceneItemVisible(int index, bool visible);
    void deleteSceneItem(int index);
    void setPrimitiveCullMode(int sceneIndex, int meshIndex, int primitiveIndex, const QString& cullMode);
    void setPrimitiveForceAlphaOne(int sceneIndex, int meshIndex, int primitiveIndex, bool enabled);
    void relocateSceneItemInFrontOfCamera(int index);
    void focusSceneItem(int index);
    void setSceneChangedCallback(std::function<void(const QList<SceneItem>&)> callback);
    void setCameraChangedCallback(std::function<void()> callback);

protected:
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void ensureViewportInitialized();
    void renderFrame();
    void embedNativeWindow();
    void syncEmbeddedWindowGeometry();
    void notifySceneChanged();
    void notifyCameraChangedIfNeeded();

    QTimer m_renderTimer;
    bool m_initialized = false;
    bool m_initScheduled = false;
    bool m_hasEmittedCameraState = false;
    QLabel* m_statusLabel = nullptr;
    std::function<void(const QList<SceneItem>&)> m_sceneChangedCallback;
    std::function<void()> m_cameraChangedCallback;
    QVector3D m_lastEmittedCameraPosition;
    QVector3D m_lastEmittedCameraRotation;
};

}  // namespace motive::ui
