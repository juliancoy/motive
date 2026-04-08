#pragma once

#include <QHash>
#include <QPoint>
#include <QPixmap>
#include <QString>
#include <QWidget>

// Asset selection structure (moved from asset_browser_types.h)
struct AssetBrowserSelection
{
    QString filePath;
    QString directoryPath;
    QString displayName;
    bool isDirectory = false;
};
#include <functional>

class QFileSystemModel;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QStackedWidget;
class QToolButton;
class QTreeView;

namespace motive::ui {

class AssetBrowserWidget : public QWidget
{
public:
    explicit AssetBrowserWidget(QWidget* parent = nullptr);
    ~AssetBrowserWidget() override;

    void setRootPath(const QString& path);
    QString rootPath() const;
    QString galleryPath() const;
    QString selectedAssetPath() const;
    void restoreGalleryPath(const QString& path);
    void setPreviewAnchorWidget(QWidget* widget);
    void setActivationCallback(std::function<void(const AssetBrowserSelection&)> callback);
    void setRootPathChangedCallback(std::function<void(const QString&)> callback);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void buildUi();
    QWidget* buildTreePage();
    QWidget* buildGalleryPage();
    void setGalleryPath(const QString& path);
    void populateGallery();
    QPixmap previewPixmapForFile(const QString& filePath);
    void showHoverPreview(const QString& filePath);
    void hideHoverPreview();
    void activatePath(const QString& filePath);

    QString m_rootPath;
    QString m_galleryPath;
    AssetBrowserSelection m_lastActivated;
    std::function<void(const AssetBrowserSelection&)> m_activationCallback;
    std::function<void(const QString&)> m_rootPathChangedCallback;
    QWidget* m_previewAnchorWidget = nullptr;
    QFileSystemModel* m_fsModel = nullptr;
    QTreeView* m_tree = nullptr;
    QStackedWidget* m_stack = nullptr;
    QListWidget* m_galleryList = nullptr;
    QLabel* m_rootPathLabel = nullptr;
    QLabel* m_galleryTitleLabel = nullptr;
    QLabel* m_hoverPreview = nullptr;
    QPushButton* m_rootButton = nullptr;
    QToolButton* m_refreshButton = nullptr;
    QToolButton* m_galleryBackButton = nullptr;
    QHash<QString, QPixmap> m_previewPixmapCache;
    QPoint m_treeDragStartPos;
    QPoint m_galleryDragStartPos;
};

}  // namespace motive::ui
