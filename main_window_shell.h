#pragma once

#include "project_session.h"
#include "viewport_host_widget.h"

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
#include <QStringList>
#include <QSplitter>

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
    QString vectorText(const QVector3D& value) const;
    QDoubleSpinBox* createSpinBox(QWidget* parent, double min, double max, double step);

    AssetBrowserWidget* m_assetBrowser = nullptr;
    ViewportHostWidget* m_viewportHost = nullptr;
    QWidget* m_leftPane = nullptr;
    QSplitter* m_splitter = nullptr;
    QTabWidget* m_rightTabs = nullptr;
    QTreeWidget* m_hierarchyTree = nullptr;
    QLabel* m_inspectorNameValue = nullptr;
    QLabel* m_inspectorPathValue = nullptr;
    QLabel* m_animationModeValue = nullptr;
    QLabel* m_inspectorTexturePreview = nullptr;
    QComboBox* m_primitiveCullModeCombo = nullptr;
    QPushButton* m_primitiveForceAlphaButton = nullptr;
    QCheckBox* m_loadMeshConsolidationCheck = nullptr;
    QCheckBox* m_paintOverrideCheck = nullptr;
    QWidget* m_paintColorWidget = nullptr;
    QWidget* m_animationControlsWidget = nullptr;
    QComboBox* m_animationClipCombo = nullptr;
    QCheckBox* m_animationPlayingCheck = nullptr;
    QCheckBox* m_animationLoopCheck = nullptr;
    QDoubleSpinBox* m_animationSpeedSpin = nullptr;
    QWidget* m_lightTypeWidget = nullptr;
    QComboBox* m_lightTypeCombo = nullptr;
    QDoubleSpinBox* m_lightBrightnessSpin = nullptr;
    QWidget* m_lightColorWidget = nullptr;
    QDoubleSpinBox* m_inspectorTranslationX = nullptr;
    QDoubleSpinBox* m_inspectorTranslationY = nullptr;
    QDoubleSpinBox* m_inspectorTranslationZ = nullptr;
    QDoubleSpinBox* m_inspectorRotationX = nullptr;
    QDoubleSpinBox* m_inspectorRotationY = nullptr;
    QDoubleSpinBox* m_inspectorRotationZ = nullptr;
    QDoubleSpinBox* m_inspectorScaleX = nullptr;
    QDoubleSpinBox* m_inspectorScaleY = nullptr;
    QDoubleSpinBox* m_inspectorScaleZ = nullptr;

    QDoubleSpinBox* m_cameraSpeedSpin = nullptr;
    QComboBox* m_renderPathCombo = nullptr;
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
};

}  // namespace motive::ui
