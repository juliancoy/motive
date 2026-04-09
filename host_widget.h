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
#include <memory>

class QLabel;
class QDragEnterEvent;
class QDropEvent;
class QFocusEvent;
class QMouseEvent;
class QMoveEvent;
class QResizeEvent;
class QShowEvent;

namespace motive {
class IPhysicsBody;
}

namespace motive::ui {

class ViewportRuntime;
class ViewportSceneController;
class ViewportCameraController;
class ViewportHierarchyBuilder;

class ViewportHostWidget : public QWidget
{
public:
    // Camera configuration (persistent camera settings)
    struct CameraConfig
    {
        enum class Type
        {
            Free,    // Free-flying camera with position/rotation
            Follow   // Camera that follows a scene item
        };
        
        QString name = QStringLiteral("Camera");
        Type type = Type::Free;
        
        // For Free cameras: position and rotation
        QVector3D position = QVector3D(0.0f, 0.0f, 3.0f);
        QVector3D rotation = QVector3D(0.0f, 0.0f, 0.0f);  // Euler angles in degrees
        
        // For Follow cameras: which scene item to follow
        int followTargetIndex = -1;  // -1 means no target (behaves like free camera)
        float followDistance = 5.0f;
        float followYaw = 0.0f;      // Horizontal angle offset (degrees)
        float followPitch = 20.0f;   // Vertical angle offset (degrees)
        float followSmoothSpeed = 5.0f;
        QVector3D followTargetOffset = QVector3D(0.0f, 0.0f, 0.0f);
        
        bool isFollowCamera() const { return type == Type::Follow && followTargetIndex >= 0; }
    };
    
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
        
        // Animation-Physics Coupling
        QString animationPhysicsCoupling = QStringLiteral("AnimationOnly");  // Default to animation only
        
        // Per-object Physics Gravity
        bool useGravity = true;  // Use world gravity by default
        QVector3D customGravity = QVector3D(0.0f, 0.0f, 0.0f);  // Zero = use world gravity
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
        int cameraIndex = -1;  // Index into Display::cameras for Camera type nodes
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
    void updateSceneItemAnimationPhysicsCoupling(int index, const QString& couplingMode);
    void updateSceneItemPhysicsGravity(int index, bool useGravity, const QVector3D& customGravity);
    void renameSceneItem(int index, const QString& name);
    void setSceneItemVisible(int index, bool visible);
    void deleteSceneItem(int index);
    void setPrimitiveCullMode(int sceneIndex, int meshIndex, int primitiveIndex, const QString& cullMode);
    void setPrimitiveForceAlphaOne(int sceneIndex, int meshIndex, int primitiveIndex, bool enabled);
    void relocateSceneItemInFrontOfCamera(int index);
    void focusSceneItem(int index);
    void setSceneChangedCallback(std::function<void(const QList<SceneItem>&)> callback);
    void setCameraChangedCallback(std::function<void()> callback);
    
    // Performance profiling
    struct PerformanceMetrics
    {
        float currentFps = 0.0f;
        int renderIntervalMs = 16;
        bool renderTimerActive = false;
        int viewportWidth = 0;
        int viewportHeight = 0;
    };
    PerformanceMetrics performanceMetrics() const;
    
    // Character controller setup
    void enableCharacterControl(int sceneIndex, bool enabled);
    bool isCharacterControlEnabled(int sceneIndex) const;
    
    // Physics body access for scene items
    motive::IPhysicsBody* getPhysicsBodyForSceneItem(int sceneIndex) const;
    
    // Camera mode
    void setFreeFlyCameraEnabled(bool enabled);
    bool isFreeFlyCameraEnabled() const;
    
    // Camera management (persistent cameras)
    QList<CameraConfig> cameraConfigs() const;
    void setCameraConfigs(const QList<CameraConfig>& configs);
    
    // Create a follow camera for a scene item
    // Returns the index of the created camera in cameraConfigs()
    int createFollowCamera(int sceneIndex, float distance = 5.0f, float yaw = 0.0f, float pitch = 20.0f);
    
    // Delete a camera by index
    void deleteCamera(int cameraIndex);
    
    // Get/set active camera index
    int activeCameraIndex() const;
    void setActiveCamera(int cameraIndex);
    
    // Update camera configuration
    void updateCameraConfig(int cameraIndex, const CameraConfig& config);
    
    // Refresh/rebuild the viewport and hierarchy
    void refresh();

protected:
    void showEvent(QShowEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void ensureViewportInitialized();
    void renderFrame();
    void notifySceneChanged();
    void notifyCameraChangedIfNeeded();
    void applySceneLightToRuntime();
    void updateCameraFollowCharacter(float dt);  // Third-person camera follow
    void updateFollowCameras(float dt);  // Update all active follow cameras

    QTimer m_renderTimer;
    bool m_initialized = false;
    bool m_initScheduled = false;
    bool m_hasEmittedCameraState = false;
    bool m_freeFlyCameraEnabled = true;  // Default to free fly mode
    QLabel* m_statusLabel = nullptr;
    
    // Camera orbit control (for character follow mode)
    bool m_orbiting = false;
    QPoint m_lastMousePos;
    float m_orbitYaw = 0.0f;      // Horizontal orbit angle
    float m_orbitPitch = 0.3f;    // Vertical orbit angle (start slightly above)
    float m_orbitDistance = 3.0f; // Distance from character
    static constexpr float kMinOrbitDistance = 1.5f;
    static constexpr float kMaxOrbitDistance = 10.0f;
    std::function<void(const QList<SceneItem>&)> m_sceneChangedCallback;
    std::function<void()> m_cameraChangedCallback;
    QVector3D m_lastEmittedCameraPosition;
    QVector3D m_lastEmittedCameraRotation;
    SceneLight m_sceneLight;

    std::unique_ptr<ViewportRuntime> m_runtime;
    std::unique_ptr<ViewportSceneController> m_sceneController;
    std::unique_ptr<ViewportCameraController> m_cameraController;
    std::unique_ptr<ViewportHierarchyBuilder> m_hierarchyBuilder;
};

}  // namespace motive::ui
