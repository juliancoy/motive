#pragma once

#include "project_session.h"
#include "host_widget.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QCheckBox>
#include <QDockWidget>
#include <QImage>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTreeWidget>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QStringList>
#include <QSplitter>
#include <QUndoStack>

namespace motive::ui {

class AssetBrowserWidget;

class MainWindowShell : public QMainWindow
{
public:
    explicit MainWindowShell(QWidget* parent = nullptr);
    ~MainWindowShell() override;

    AssetBrowserWidget* assetBrowser() const;
    ViewportHostWidget* viewportHost() const;
    QJsonArray hierarchyJson() const;
    QJsonObject uiDebugJson() const;
    QJsonObject uiWidgetTreeJson() const;
    QJsonObject inspectorDebugJson() const;
    bool selectHierarchySceneItem(int sceneIndex);
    bool selectHierarchyCamera(const QString& cameraId, int cameraIndex = -1);
    bool selectHierarchyLight();

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    static constexpr int kHierarchyCameraIndex = -1000;
    static constexpr int kHierarchyLightIndex = -1001;

    static QJsonObject sceneLightToJson(const ViewportHostWidget::SceneLight& light);
    static ViewportHostWidget::SceneLight sceneLightFromJson(const QJsonObject& object);

    void restoreSessionState();
    void setupProjectMenu();
    void refreshWindowTitle();
    void createProject();
    void switchProject();
    void setMediaDirProjects();
    int selectedSceneItemIndex() const;
    void exportSelectedSceneItemToGltf();
    void saveProjectState();
    void saveUiState();
    QJsonObject captureUiState() const;
    void applyUiState(const QJsonObject& state);
    void maybePromptForGltfConversion(const QString& rootPath);
    void refreshHierarchy(const QList<ViewportHostWidget::SceneItem>& items);
    void appendHierarchyNode(QTreeWidgetItem* parent, const ViewportHostWidget::HierarchyNode& node, bool ancestorHidden = false);
    void updateInspectorForSelection(QTreeWidgetItem* item, bool focusContextTab = false);
    void configureElementInspectorForSelection(int nodeType,
                                               int sceneIndex,
                                               int meshIndex,
                                               int primitiveIndex,
                                               bool hasAnimation,
                                               bool isTextItem,
                                               bool focusContextTab);
    void setupCameraSettingsPanel();
    void updateCameraSettingsPanel();
    void applyCameraSettings();
    void updateWasdRoutingStatus();
    void refreshProfileAvatarButton();
    void onProfileAvatarClicked();
    QJsonArray sceneItemsToJson(const QList<ViewportHostWidget::SceneItem>& items) const;
    QList<ViewportHostWidget::SceneItem> sceneItemsFromJson(const QJsonArray& items) const;
    QJsonArray cameraConfigsToJson(const QList<ViewportHostWidget::CameraConfig>& configs) const;
    QList<ViewportHostWidget::CameraConfig> cameraConfigsFromJson(const QJsonArray& configs) const;
    QString vectorText(const QVector3D& value) const;
    QDoubleSpinBox* createSpinBox(QWidget* parent, double min, double max, double step);
    QWidget* wrapTabInScrollArea(QWidget* content) const;

    AssetBrowserWidget* m_assetBrowser = nullptr;
    ViewportHostWidget* m_viewportHost = nullptr;
    QWidget* m_leftPane = nullptr;
    QSplitter* m_splitter = nullptr;
    QSplitter* m_leftVerticalSplitter = nullptr;
    QDockWidget* m_inspectorDock = nullptr;
    QTabWidget* m_rightTabs = nullptr;
    QPushButton* m_profileAvatarButton = nullptr;
    QString m_authApiBaseUrl;
    QTabWidget* m_elementDetailTabs = nullptr;
    QTreeWidget* m_hierarchyTree = nullptr;
    QGroupBox* m_summarySection = nullptr;
    QGroupBox* m_materialSection = nullptr;
    QGroupBox* m_animationSection = nullptr;
    QGroupBox* m_cameraSection = nullptr;
    QGroupBox* m_lightSection = nullptr;
    QGroupBox* m_transformSection = nullptr;
    QGroupBox* m_placementSection = nullptr;
    QGroupBox* m_physicsSection = nullptr;
    QGroupBox* m_runtimeSection = nullptr;
    QGroupBox* m_motionDebugOverlaySection = nullptr;
    QGroupBox* m_textSection = nullptr;
    QLabel* m_inspectorNameValue = nullptr;
    QLabel* m_inspectorPathValue = nullptr;
    QLabel* m_animationModeValue = nullptr;
    QLabel* m_cameraTypeValue = nullptr;
    QLabel* m_boundsSizeValue = nullptr;
    QLabel* m_boundsCenterValue = nullptr;
    QLabel* m_boundsMinValue = nullptr;
    QLabel* m_boundsMaxValue = nullptr;
    QLabel* m_inspectorTexturePreview = nullptr;
    QComboBox* m_primitiveCullModeCombo = nullptr;
    QPushButton* m_primitiveForceAlphaButton = nullptr;
    QCheckBox* m_loadMeshConsolidationCheck = nullptr;
    QCheckBox* m_paintOverrideCheck = nullptr;
    QWidget* m_paintColorWidget = nullptr;
    QWidget* m_paintColorContainer = nullptr;
    QWidget* m_animationControlsWidget = nullptr;
    QWidget* m_characterBindingsWidget = nullptr;
    QLabel* m_animationClipSummaryValue = nullptr;
    QComboBox* m_animationClipCombo = nullptr;
    QCheckBox* m_animationPlayingCheck = nullptr;
    QCheckBox* m_animationLoopCheck = nullptr;
    QDoubleSpinBox* m_animationSpeedSpin = nullptr;
    QCheckBox* m_animationCentroidNormalizeCheck = nullptr;
    QDoubleSpinBox* m_animationTrimStartSpin = nullptr;
    QDoubleSpinBox* m_animationTrimEndSpin = nullptr;
    QComboBox* m_characterIdleClipCombo = nullptr;
    QComboBox* m_characterComeToRestClipCombo = nullptr;
    QComboBox* m_characterWalkForwardClipCombo = nullptr;
    QComboBox* m_characterWalkBackwardClipCombo = nullptr;
    QComboBox* m_characterWalkLeftClipCombo = nullptr;
    QComboBox* m_characterWalkRightClipCombo = nullptr;
    QComboBox* m_characterRunClipCombo = nullptr;
    QComboBox* m_characterJumpClipCombo = nullptr;
    QComboBox* m_characterFallClipCombo = nullptr;
    QComboBox* m_characterLandClipCombo = nullptr;
    
    // Animation-Physics Coupling
    QComboBox* m_animationPhysicsCouplingCombo = nullptr;
    
    // Per-object Physics Gravity
    QCheckBox* m_elementUseGravityCheck = nullptr;
    QDoubleSpinBox* m_elementGravityX = nullptr;
    QDoubleSpinBox* m_elementGravityY = nullptr;
    QDoubleSpinBox* m_elementGravityZ = nullptr;
    QWidget* m_elementGravityWidget = nullptr;
    QDoubleSpinBox* m_characterTurnResponsivenessSpin = nullptr;
    QDoubleSpinBox* m_characterMoveSpeedSpin = nullptr;
    QDoubleSpinBox* m_characterIdleAnimationSpeedSpin = nullptr;
    QDoubleSpinBox* m_characterWalkAnimationSpeedSpin = nullptr;
    QCheckBox* m_characterProceduralIdleCheck = nullptr;
    QLabel* m_objectFollowCamInfoValue = nullptr;
    QLabel* m_objectKinematicInfoValue = nullptr;
    QLabel* m_objectAnimationRuntimeInfoValue = nullptr;
    QCheckBox* m_motionOverlayEnabledCheck = nullptr;
    QCheckBox* m_motionOverlayTargetMarkersCheck = nullptr;
    QCheckBox* m_motionOverlayVelocityCheck = nullptr;
    QCheckBox* m_motionOverlayCameraLineCheck = nullptr;
    QCheckBox* m_motionOverlayCenterCrosshairCheck = nullptr;
    QDoubleSpinBox* m_motionOverlayVelocityScaleSpin = nullptr;
    QLineEdit* m_textContentEdit = nullptr;
    QLineEdit* m_textFontPathEdit = nullptr;
    QSpinBox* m_textPixelHeightSpin = nullptr;
    QCheckBox* m_textBoldCheck = nullptr;
    QCheckBox* m_textItalicCheck = nullptr;
    QCheckBox* m_textShadowCheck = nullptr;
    QCheckBox* m_textOutlineCheck = nullptr;
    QSpinBox* m_textLetterSpacingSpin = nullptr;
    QDoubleSpinBox* m_textExtrudeDepthSpin = nullptr;
    QCheckBox* m_textExtrudeGlyphsOnlyCheck = nullptr;
    QWidget* m_textColorSwatch = nullptr;
    QWidget* m_textBgColorSwatch = nullptr;
    QCheckBox* m_textDepthTestCheck = nullptr;
    QCheckBox* m_textDepthWriteCheck = nullptr;
    
    QWidget* m_lightTypeWidget = nullptr;
    QComboBox* m_lightTypeCombo = nullptr;
    QDoubleSpinBox* m_lightBrightnessSpin = nullptr;
    QWidget* m_lightColorWidget = nullptr;
    QWidget* m_lightColorContainer = nullptr;
    QPushButton* m_lightFocusButton = nullptr;
    QWidget* m_translationWidget = nullptr;
    QWidget* m_rotationWidget = nullptr;
    QWidget* m_scaleWidget = nullptr;
    QDoubleSpinBox* m_inspectorTranslationX = nullptr;
    QDoubleSpinBox* m_inspectorTranslationY = nullptr;
    QDoubleSpinBox* m_inspectorTranslationZ = nullptr;
    QDoubleSpinBox* m_inspectorRotationX = nullptr;
    QDoubleSpinBox* m_inspectorRotationY = nullptr;
    QDoubleSpinBox* m_inspectorRotationZ = nullptr;
    QDoubleSpinBox* m_inspectorScaleX = nullptr;
    QDoubleSpinBox* m_inspectorScaleY = nullptr;
    QDoubleSpinBox* m_inspectorScaleZ = nullptr;
    QCheckBox* m_lockScaleXYZCheck = nullptr;
    QPushButton* m_alignBottomToGroundButton = nullptr;
    QComboBox* m_placementTargetCombo = nullptr;
    QComboBox* m_placementLandmarkCombo = nullptr;
    QPushButton* m_placementApplyButton = nullptr;
    QLabel* m_placementLandmarksValue = nullptr;

    // Camera follow target selector
    QComboBox* m_followTargetCombo = nullptr;
    QLabel* m_followTargetLabel = nullptr;
    
    // Follow camera specific controls
    QDoubleSpinBox* m_followDistanceSpin = nullptr;
    QDoubleSpinBox* m_followYawSpin = nullptr;
    QDoubleSpinBox* m_followPitchSpin = nullptr;
    QDoubleSpinBox* m_followSmoothSpin = nullptr;
    QLabel* m_followParamsLabel = nullptr;

    QDoubleSpinBox* m_cameraSpeedSpin = nullptr;
    QDoubleSpinBox* m_nearClipSpin = nullptr;
    QDoubleSpinBox* m_farClipSpin = nullptr;
    QComboBox* m_renderPathCombo = nullptr;
    QComboBox* m_viewportCountCombo = nullptr;
    QComboBox* m_globalPhysicsEngineCombo = nullptr;
    QDoubleSpinBox* m_globalGravityXSpin = nullptr;
    QDoubleSpinBox* m_globalGravityYSpin = nullptr;
    QDoubleSpinBox* m_globalGravityZSpin = nullptr;
    QSpinBox* m_globalPhysicsMaxSubStepsSpin = nullptr;
    QCheckBox* m_globalPhysicsDebugDrawCheck = nullptr;
    QCheckBox* m_globalPhysicsAutoSyncCheck = nullptr;
    QCheckBox* m_meshConsolidationCheck = nullptr;
    QCheckBox* m_validationLayersCheck = nullptr;
    QLabel* m_validationRestartLabel = nullptr;
    QComboBox* m_wasdRoutingCombo = nullptr;
    QCheckBox* m_freeFlyCameraCheck = nullptr;
    QLabel* m_wasdRoutingStatusValue = nullptr;
    QPushButton* m_takeWasdControlButton = nullptr;
    QPushButton* m_resetControlRoutingButton = nullptr;
    QPushButton* m_saveProjectButton = nullptr;
    QCheckBox* m_invertHorizontalDragCheck = nullptr;
    QWidget* m_bgColorWidget = nullptr;

    ProjectSession m_projectSession;
    QList<ViewportHostWidget::SceneItem> m_sceneItems;
    QStringList m_promptedConversionRoots;
    bool m_restoringSessionState = false;
    bool m_savingUiState = false;
    bool m_updatingInspector = false;
    bool m_updatingCameraSettings = false;
    bool m_suppressHierarchySelectionEffects = false;
    
    // Undo stack for scene operations
    QUndoStack* m_undoStack = nullptr;
    
    // Helper to push transform changes to undo stack
    void pushTransformCommand(
        int sceneIndex,
        const QVector3D& oldTranslation,
        const QVector3D& oldRotation,
        const QVector3D& oldScale,
        const QVector3D& newTranslation,
        const QVector3D& newRotation,
        const QVector3D& newScale);
    
    void setupUndoShortcuts();
    void centerAllSceneItemsToOrigin();
};

}  // namespace motive::ui
