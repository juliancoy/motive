#pragma once

#include "project_session.h"
#include "host_widget.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QCheckBox>
#include <QImage>
#include <QJsonArray>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
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
    QJsonObject inspectorDebugJson() const;
    bool selectHierarchySceneItem(int sceneIndex);
    bool selectHierarchyCamera(const QString& cameraId, int cameraIndex = -1);

protected:
    void closeEvent(QCloseEvent* event) override;

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
    void saveProjectState();
    void saveUiState();
    void maybePromptForGltfConversion(const QString& rootPath);
    void refreshHierarchy(const QList<ViewportHostWidget::SceneItem>& items);
    void appendHierarchyNode(QTreeWidgetItem* parent, const ViewportHostWidget::HierarchyNode& node, bool ancestorHidden = false);
    void updateInspectorForSelection(QTreeWidgetItem* item);
    void setupCameraSettingsPanel();
    void updateCameraSettingsPanel();
    void applyCameraSettings();
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
    QTabWidget* m_rightTabs = nullptr;
    QTabWidget* m_elementDetailTabs = nullptr;
    QTreeWidget* m_hierarchyTree = nullptr;
    QGroupBox* m_summarySection = nullptr;
    QGroupBox* m_materialSection = nullptr;
    QGroupBox* m_animationSection = nullptr;
    QGroupBox* m_cameraSection = nullptr;
    QGroupBox* m_lightSection = nullptr;
    QGroupBox* m_transformSection = nullptr;
    QGroupBox* m_physicsSection = nullptr;
    QGroupBox* m_runtimeSection = nullptr;
    QLabel* m_inspectorNameValue = nullptr;
    QLabel* m_inspectorPathValue = nullptr;
    QLabel* m_animationModeValue = nullptr;
    QLabel* m_boundsSizeValue = nullptr;
    QLabel* m_inspectorTexturePreview = nullptr;
    QComboBox* m_primitiveCullModeCombo = nullptr;
    QPushButton* m_primitiveForceAlphaButton = nullptr;
    QCheckBox* m_loadMeshConsolidationCheck = nullptr;
    QCheckBox* m_paintOverrideCheck = nullptr;
    QWidget* m_paintColorWidget = nullptr;
    QWidget* m_paintColorContainer = nullptr;
    QWidget* m_animationControlsWidget = nullptr;
    QComboBox* m_animationClipCombo = nullptr;
    QCheckBox* m_animationPlayingCheck = nullptr;
    QCheckBox* m_animationLoopCheck = nullptr;
    QDoubleSpinBox* m_animationSpeedSpin = nullptr;
    
    // Animation-Physics Coupling
    QComboBox* m_animationPhysicsCouplingCombo = nullptr;
    
    // Per-object Physics Gravity
    QCheckBox* m_elementUseGravityCheck = nullptr;
    QDoubleSpinBox* m_elementGravityX = nullptr;
    QDoubleSpinBox* m_elementGravityY = nullptr;
    QDoubleSpinBox* m_elementGravityZ = nullptr;
    QWidget* m_elementGravityWidget = nullptr;
    QDoubleSpinBox* m_characterTurnResponsivenessSpin = nullptr;
    QLabel* m_objectFollowCamInfoValue = nullptr;
    QLabel* m_objectKinematicInfoValue = nullptr;
    QLabel* m_objectAnimationRuntimeInfoValue = nullptr;
    
    QWidget* m_lightTypeWidget = nullptr;
    QComboBox* m_lightTypeCombo = nullptr;
    QDoubleSpinBox* m_lightBrightnessSpin = nullptr;
    QWidget* m_lightColorWidget = nullptr;
    QWidget* m_lightColorContainer = nullptr;
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
    QCheckBox* m_meshConsolidationCheck = nullptr;
    QCheckBox* m_validationLayersCheck = nullptr;
    QLabel* m_validationRestartLabel = nullptr;
    QCheckBox* m_freeFlyCameraCheck = nullptr;
    QWidget* m_bgColorWidget = nullptr;

    ProjectSession m_projectSession;
    QList<ViewportHostWidget::SceneItem> m_sceneItems;
    QStringList m_promptedConversionRoots;
    bool m_restoringSessionState = false;
    bool m_savingUiState = false;
    bool m_updatingInspector = false;
    bool m_updatingCameraSettings = false;
    
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
};

}  // namespace motive::ui
