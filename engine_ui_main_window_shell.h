#pragma once

#include "engine_ui_project_session.h"
#include "engine_ui_viewport_host_widget.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QJsonArray>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QTabWidget>
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

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void restoreSessionState();
    void setupProjectMenu();
    void refreshWindowTitle();
    void createProject();
    void switchProject();
    void saveProjectState();
    void saveUiState();
    void maybePromptForGltfConversion(const QString& rootPath);
    void refreshHierarchy(const QList<ViewportHostWidget::SceneItem>& items);
    void updateInspectorForSelection(int row);
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
    QListWidget* m_hierarchyList = nullptr;
    QLabel* m_inspectorNameValue = nullptr;
    QLabel* m_inspectorPathValue = nullptr;
    QDoubleSpinBox* m_inspectorTranslationX = nullptr;
    QDoubleSpinBox* m_inspectorTranslationY = nullptr;
    QDoubleSpinBox* m_inspectorTranslationZ = nullptr;
    QDoubleSpinBox* m_inspectorRotationX = nullptr;
    QDoubleSpinBox* m_inspectorRotationY = nullptr;
    QDoubleSpinBox* m_inspectorRotationZ = nullptr;
    QDoubleSpinBox* m_inspectorScaleX = nullptr;
    QDoubleSpinBox* m_inspectorScaleY = nullptr;
    QDoubleSpinBox* m_inspectorScaleZ = nullptr;
    
    // Camera/Scene settings
    QDoubleSpinBox* m_cameraSpeedSpin = nullptr;
    QDoubleSpinBox* m_cameraPosX = nullptr;
    QDoubleSpinBox* m_cameraPosY = nullptr;
    QDoubleSpinBox* m_cameraPosZ = nullptr;
    QDoubleSpinBox* m_cameraRotX = nullptr;
    QDoubleSpinBox* m_cameraRotY = nullptr;
    QDoubleSpinBox* m_cameraRotZ = nullptr;
    QComboBox* m_renderPathCombo = nullptr;
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
