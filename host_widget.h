#pragma once

#include <QColor>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QRect>
#include <QTimer>
#include <QVector3D>
#include <QWidget>
#include <QString>
#include <QStringList>
#include "text_rendering.h"
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <memory>

class QLabel;
class QComboBox;
class QFrame;
class QGridLayout;
class QDragEnterEvent;
class QDropEvent;
class QFocusEvent;
class QMouseEvent;
class QMoveEvent;
class QResizeEvent;
class QShowEvent;
class Camera;

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
    struct CameraConfig
    {
        enum class Type
        {
            Free,
            Follow
        };

        QString id;
        QString name = QStringLiteral("Camera");
        Type type = Type::Free;
        QVector3D position = QVector3D(0.0f, 0.0f, 3.0f);
        QVector3D rotation = QVector3D(0.0f, 0.0f, 0.0f);
        int followTargetIndex = -1;
        float followDistance = 5.0f;
        float followYaw = 0.0f;
        float followPitch = 20.0f;
        float followSmoothSpeed = 10.0f;
        QVector3D followTargetOffset = QVector3D(0.0f, 0.0f, 0.0f);
        QString mode = QStringLiteral("FreeFly");
        bool freeFly = true;
        bool invertHorizontalDrag = false;
        float nearClip = 0.1f;
        float farClip = 100.0f;
        bool isFollowCamera() const { return type == Type::Follow && followTargetIndex >= 0; }
        bool isFreeFlyCamera() const { return freeFly; }
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
        bool animationCentroidNormalization = true;
        float animationTrimStartNormalized = 0.0f;
        float animationTrimEndNormalized = 1.0f;
        bool visible = true;
        QString animationPhysicsCoupling = QStringLiteral("AnimationOnly");
        bool useGravity = true;
        QVector3D customGravity = QVector3D(0.0f, 0.0f, 0.0f);
        float characterTurnResponsiveness = 10.0f;
        QJsonArray primitiveOverrides;
        QVector3D focusPointOffset = QVector3D(0.0f, 0.0f, 0.0f);
        float focusDistance = 0.0f; // <= 0 uses automatic framing distance
        QVector3D focusCameraOffset = QVector3D(0.0f, 0.0f, 0.0f);
        bool focusCameraOffsetValid = false;
        bool characterRestPointOnReleaseEnabled = true;
        float characterRestPointOnReleaseNormalized = 1.0f;
        QString textContent = QStringLiteral("Text");
        QString textFontPath;
        int textPixelHeight = 56;
        bool textBold = false;
        bool textItalic = false;
        bool textShadow = true;
        bool textOutline = false;
        int textLetterSpacing = 0;
        QString textColor = QStringLiteral("#ffffffff");
        QString textBackgroundColor = QStringLiteral("#aa000000");
        float textExtrudeDepth = 0.02f;
        bool textExtrudeGlyphsOnly = true;
        bool textDepthTest = false;
        bool textDepthWrite = false;
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
        int cameraIndex = -1;
        QString cameraId;
        int primitiveIndex = -1;
        QString clipName;
        QList<HierarchyNode> children;
    };

    struct ViewportLayout
    {
        int count = 1;
        QStringList cameraIds;
    };

    explicit ViewportHostWidget(QWidget* parent = nullptr);
    ~ViewportHostWidget() override;

    void loadAssetFromPath(const QString& path);
    void loadSceneFromItems(const QList<SceneItem>& items);
    void addAssetToScene(const QString& path);
    void addTextOverlayToScene();
    QString currentAssetPath() const;
    QList<SceneItem> sceneItems() const;
    QList<HierarchyNode> hierarchyItems() const;
    QJsonArray hierarchyJson() const;
    QJsonArray sceneProfileJson() const;
    QJsonObject cameraTrackingDebugJson() const;
    QJsonObject motionDebugFrameJson() const;
    QJsonArray motionDebugHistoryJson(int maxFrames = 300, int sceneIndex = -1) const;
    QJsonObject motionDebugSummaryJson() const;
    void resetMotionDebug();
    QVector3D sceneItemBoundsSize(int sceneIndex) const;
    QVector3D sceneItemBoundsCenter(int sceneIndex) const;
    QVector3D sceneItemBoundsMin(int sceneIndex) const;
    QVector3D sceneItemBoundsMax(int sceneIndex) const;
    QImage primitiveTexturePreview(int sceneIndex, int meshIndex, int primitiveIndex) const;
    QString animationExecutionMode(int sceneIndex, int meshIndex = -1, int primitiveIndex = -1) const;
    QString primitiveCullMode(int sceneIndex, int meshIndex, int primitiveIndex) const;
    QString sceneItemCullMode(int sceneIndex) const;
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
    void setPerspectiveNearFar(float near, float far);
    void getPerspectiveNearFar(float& near, float& far) const;
    void resetCamera();
    void setBackgroundColor(const QColor& color);
    void setRenderPath(const QString& renderPath);
    void setMeshConsolidationEnabled(bool enabled);
    void createSceneLight();
    void setSceneLight(const SceneLight& light);
    void updateSceneItemTransform(int index, const QVector3D& translation, const QVector3D& rotation, const QVector3D& scale);
    bool alignSceneItemBottomToGround(int index, float groundY = 0.0f);
    void setSceneItemMeshConsolidationEnabled(int index, bool enabled);
    void updateSceneItemPaintOverride(int index, bool enabled, const QVector3D& color);
    void updateSceneItemAnimationState(int index, const QString& activeClip, bool playing, bool loop, float speed);
    void updateSceneItemAnimationProcessing(int index,
                                            bool centroidNormalizationEnabled,
                                            float trimStartNormalized,
                                            float trimEndNormalized);
    void updateSceneItemAnimationPhysicsCoupling(int index, const QString& couplingMode);
    void updateSceneItemPhysicsGravity(int index, bool useGravity, const QVector3D& customGravity);
    void updateSceneItemCharacterTurnResponsiveness(int index, float responsiveness);
    void updateSceneItemCharacterRestPointOnRelease(int index, bool enabled, float normalized);
    void updateSceneItemFocusSettings(int index, const QVector3D& focusPointOffset, float focusDistance);
    void captureSceneItemFocusFromCurrentCamera(int index);
    void renameSceneItem(int index, const QString& name);
    void setSceneItemVisible(int index, bool visible);
    void deleteSceneItem(int index);
    void setPrimitiveCullMode(int sceneIndex, int meshIndex, int primitiveIndex, const QString& cullMode, bool notify = true);
    void setSceneItemCullMode(int sceneIndex, const QString& cullMode);
    void setPrimitiveForceAlphaOne(int sceneIndex, int meshIndex, int primitiveIndex, bool enabled);
    void setCoordinatePlaneIndicatorsEnabled(bool enabled);
    bool coordinatePlaneIndicatorsEnabled() const;
    void relocateSceneItemInFrontOfCamera(int index);
    void focusSceneItem(int index);
    void setSceneChangedCallback(std::function<void(const QList<SceneItem>&)> callback);
    void setCameraChangedCallback(std::function<void()> callback);
    void setViewportFocusChangedCallback(std::function<void(const QString& cameraId)> callback);

    struct PerformanceMetrics
    {
        float currentFps = 0.0f;
        int renderIntervalMs = 16;
        bool renderTimerActive = false;
        int viewportWidth = 0;
        int viewportHeight = 0;
    };

    struct MotionDebugOverlayOptions
    {
        bool enabled = false;
        bool showTargetMarkers = true;
        bool showVelocityVector = true;
        bool showCameraToTargetLine = true;
        bool showScreenCenterCrosshair = true;
        bool showMotionTrail = true;
        bool showRawTrail = false;
        int trailFrames = 32;
        float velocityScale = 0.25f;
    };

    PerformanceMetrics performanceMetrics() const;
    QJsonObject motionDebugOverlayOptionsJson() const;
    void setMotionDebugOverlayOptions(const MotionDebugOverlayOptions& options);
    void enableCharacterControl(int sceneIndex, bool enabled);
    void selectCharacterControlOwner(int sceneIndex);
    bool isCharacterControlEnabled(int sceneIndex) const;
    bool injectCharacterInput(int sceneIndex, bool keyW, bool keyA, bool keyS, bool keyD, bool jumpRequested, int durationMs);
    bool playCharacterInputPattern(int sceneIndex, const QString& pattern, int stepDurationMs, int steps, bool includeJump);
    bool bootstrapThirdPersonShooter(bool force = false);
    QJsonObject bootstrapThirdPersonShooterReport(bool force = false);
    QJsonObject thirdPersonShooterStateJson() const;
    motive::IPhysicsBody* getPhysicsBodyForSceneItem(int sceneIndex) const;
    void setFreeFlyCameraEnabled(bool enabled);
    bool isFreeFlyCameraEnabled() const;
    QList<CameraConfig> cameraConfigs() const;
    void setCameraConfigs(const QList<CameraConfig>& configs);
    ViewportLayout viewportLayout() const;
    void setViewportLayout(const ViewportLayout& layout);
    void setViewportCount(int count);
    int viewportCount() const;
    int focusedViewportIndex() const;
    QString focusedViewportCameraId() const;
    QStringList viewportCameraIds() const;
    int ensureFollowCamera(int sceneIndex, float distance = 5.0f, float yaw = 0.0f, float pitch = 20.0f);
    int createFollowCamera(int sceneIndex, float distance = 5.0f, float yaw = 0.0f, float pitch = 20.0f);
    void deleteCamera(int cameraIndex);
    int activeCameraIndex() const;
    void setActiveCamera(int cameraIndex);
    int cameraIndexForId(const QString& cameraId) const;
    QString activeCameraId() const;
    void updateCameraConfig(int cameraIndex, const CameraConfig& config);
    bool normalizeSceneScaleForMeters(float targetCharacterRadius = 0.45f);
    void refresh();
    void setCustomOverlayBitmap(const glyph::OverlayBitmap& bitmap);
    void clearCustomOverlayBitmap();
    void updateSceneItemTextOverlay(int index, const SceneItem& textProps);

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
    void updateViewportLayout();
    void syncViewportSelectorChoices();
    void layoutViewportSelectors();
    void updateViewportBorders();
    QRect viewportRectForIndex(int index) const;
    int viewportIndexAt(const QPoint& position) const;
    QString cameraIdForViewportIndex(int index) const;
    Camera* focusedViewportCamera() const;
    void setFocusedViewportIndex(int index);
    void setCharacterControlState(int sceneIndex, bool enabled, bool repositionForCharacterMode);
    void ensureFollowCamerasForAllSceneItems();
    void captureMotionDebugFrame(float dt);
    void updateMotionDebugOverlay();
    void updateCameraDirectionIndicator();

    struct MotionDebugSample
    {
        std::uint64_t frame = 0;
        double elapsedSeconds = 0.0;
        float deltaSeconds = 0.0f;
        QString cameraId;
        QString cameraName;
        QString cameraMode;
        int targetSceneIndex = -1;
        QVector3D cameraPos;
        QVector3D targetPos;
        QVector3D targetPosRaw;
        QVector3D targetPosMotion;
        QVector3D targetVelocity;
        float distanceToTarget = 0.0f;
        float frontDotToTarget = 0.0f;
        float followDistance = 0.0f;
        float followSmoothSpeed = 0.0f;
        float targetJitterMagnitude = 0.0f;
        float cameraStepMagnitude = 0.0f;
        float distanceDelta = 0.0f;
        int distanceDeltaFlipCount = 0;
        bool oscillationSuspected = false;
        QJsonArray warnings;
    };
    static constexpr int kMotionDebugHistoryCapacity = 1800;
    static constexpr int kMotionDebugSummaryWindow = 120;
    QJsonObject motionDebugSampleToJson(const MotionDebugSample& sample) const;

    QTimer m_renderTimer;
    bool m_initialized = false;
    bool m_initScheduled = false;
    bool m_tpsBootstrapPending = true;
    bool m_tpsBootstrapApplied = false;
    bool m_hasEmittedCameraState = false;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_cameraDirectionLabel = nullptr;
    QWidget* m_renderSurface = nullptr;
    QWidget* m_viewportSelectorPanel = nullptr;
    QGridLayout* m_viewportSelectorGrid = nullptr;
    bool m_orbiting = false;
    QPoint m_lastMousePos;
    float m_orbitYaw = 0.0f;
    float m_orbitPitch = 0.3f;
    float m_orbitDistance = 3.0f;
    static constexpr float kMinOrbitDistance = 1.5f;
    static constexpr float kMaxOrbitDistance = 10.0f;
    std::function<void(const QList<SceneItem>&)> m_sceneChangedCallback;
    std::function<void()> m_cameraChangedCallback;
    std::function<void(const QString&)> m_viewportFocusChangedCallback;
    QList<CameraConfig> m_pendingCameraConfigs;
    ViewportLayout m_viewportLayout;
    QList<QComboBox*> m_viewportCameraSelectors;
    QList<QFrame*> m_viewportBorders;
    int m_focusedViewportIndex = 0;
    QVector3D m_lastEmittedCameraPosition;
    QVector3D m_lastEmittedCameraRotation;
    SceneLight m_sceneLight;
    mutable std::mutex m_motionDebugMutex;
    std::deque<MotionDebugSample> m_motionDebugHistory;
    std::uint64_t m_motionDebugFrameCounter = 0;
    double m_motionDebugElapsedSeconds = 0.0;
    bool m_motionDebugHasLast = false;
    QVector3D m_motionDebugLastCameraPos = QVector3D(0.0f, 0.0f, 0.0f);
    float m_motionDebugLastDistance = 0.0f;
    float m_motionDebugLastDistanceDelta = 0.0f;
    int m_motionDebugDistanceFlipCount = 0;
    MotionDebugOverlayOptions m_motionDebugOverlayOptions;

    std::unique_ptr<ViewportRuntime> m_runtime;
    std::unique_ptr<ViewportSceneController> m_sceneController;
    std::unique_ptr<ViewportCameraController> m_cameraController;
    std::unique_ptr<ViewportHierarchyBuilder> m_hierarchyBuilder;
};

}  // namespace motive::ui
