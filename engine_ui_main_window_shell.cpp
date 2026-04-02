#include "engine_ui_main_window_shell.h"
#include "engine_ui_asset_browser_widget.h"
#include "engine_ui_viewport_host_widget.h"

#include <QDockWidget>
#include <QSplitter>
#include <QTimer>

namespace motive::ui {

MainWindowShell::MainWindowShell(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Motive Editor"));
    resize(1600, 900);

    auto* splitter = new QSplitter(this);
    m_assetBrowser = new AssetBrowserWidget(splitter);
    m_viewportHost = new ViewportHostWidget(splitter);
    m_assetBrowser->setPreviewAnchorWidget(m_viewportHost);
    m_assetBrowser->setActivationCallback([this](const AssetBrowserSelection& selection)
    {
        if (!selection.isDirectory && m_viewportHost)
        {
            m_viewportHost->loadAssetFromPath(selection.filePath);
        }
    });
    splitter->addWidget(m_assetBrowser);
    splitter->addWidget(m_viewportHost);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    setCentralWidget(splitter);

    auto* inspectorDock = new QDockWidget(QStringLiteral("Inspector"), this);
    inspectorDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, inspectorDock);

    QTimer::singleShot(0, this, [this]()
    {
        showNormal();
        raise();
        activateWindow();
    });
}

MainWindowShell::~MainWindowShell() = default;

AssetBrowserWidget* MainWindowShell::assetBrowser() const
{
    return m_assetBrowser;
}

ViewportHostWidget* MainWindowShell::viewportHost() const
{
    return m_viewportHost;
}

}  // namespace motive::ui
