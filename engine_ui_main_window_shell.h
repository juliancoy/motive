#pragma once

#include <QMainWindow>

namespace motive::ui {

class AssetBrowserWidget;
class ViewportHostWidget;

class MainWindowShell : public QMainWindow
{
public:
    explicit MainWindowShell(QWidget* parent = nullptr);
    ~MainWindowShell() override;

    AssetBrowserWidget* assetBrowser() const;
    ViewportHostWidget* viewportHost() const;

private:
    AssetBrowserWidget* m_assetBrowser = nullptr;
    ViewportHostWidget* m_viewportHost = nullptr;
};

}  // namespace motive::ui
