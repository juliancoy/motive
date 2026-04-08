#pragma once

// Forward declaration header for MainWindowShell
// Use this instead of shell.h when you only need to declare MainWindowShell methods
// This reduces compile-time coupling significantly

#include <QMainWindow>
#include <QJsonObject>
#include <QJsonArray>
#include <QList>
#include <QString>
#include <QVector3D>

// Qt forward declarations
class QCloseEvent;
class QComboBox;
class QCheckBox;
class QImage;
class QLabel;
class QPushButton;
class QTabWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QDoubleSpinBox;
class QStringList;
class QSplitter;
class QUndoStack;
class QWidget;

namespace motive::ui {

class AssetBrowserWidget;
class ViewportHostWidget;

// Scene item structures (duplicated from viewport_host_widget.h to avoid full include)
struct SceneItem;
struct HierarchyNode;
struct SceneLight;

// MainWindowShell class - only declare method signatures here
// Implementations should be in shell.cpp or use shell_impl.h for inline methods
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

    static QJsonObject sceneLightToJson(const SceneLight& light);
    static SceneLight sceneLightFromJson(const QJsonObject& object);

    void restoreSessionState();
    void setupProjectMenu();
    void refreshWindowTitle();
    void createProject();
    void switchProject();
    void saveProjectState();
    void saveUiState();
    void maybePromptForGltfConversion(const QString& rootPath);
    void refreshHierarchy(const QList<SceneItem>& items);
    void appendHierarchyNode(QTreeWidgetItem* parent, const HierarchyNode& node, bool ancestorHidden = false);
    void updateInspectorForSelection(QTreeWidgetItem* item);
    void setupCameraSettingsPanel();
    void updateCameraSettingsPanel();
    void applyCameraSettings();
    QJsonArray sceneItemsToJson(const QList<SceneItem>& items) const;
    QList<SceneItem> sceneItemsFromJson(const QJsonArray& items) const;
    QJsonArray cameraConfigsToJson(const QList<ViewportHostWidget::CameraConfig>& configs) const;
    QList<ViewportHostWidget::CameraConfig> cameraConfigsFromJson(const QJsonArray& configs) const;
    QString vectorText(const QVector3D& value) const;
    QDoubleSpinBox* createSpinBox(QWidget* parent, double min, double max, double step);

    void pushTransformCommand(
        int sceneIndex,
        const QVector3D& oldTranslation,
        const QVector3D& oldRotation,
        const QVector3D& oldScale,
        const QVector3D& newTranslation,
        const QVector3D& newRotation,
        const QVector3D& newScale);
    
    void setupUndoShortcuts();

    // Opaque pointer to implementation (PIMPL pattern)
    // This hides all the widget members from the header
    class Impl;
    Impl* m_impl;
};

} // namespace motive::ui
