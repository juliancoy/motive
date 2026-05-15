#include "profile_data_service.h"

#include "asset_browser_widget.h"
#include "host_widget.h"
#include "shell.h"

#include <QDir>

namespace motive::ui {

ProfileDataService::ProfileDataService(MainWindowShell& window)
    : m_window(window)
{
}

QString ProfileDataService::resolveRootPath() const
{
    if (auto* browser = m_window.assetBrowser())
    {
        const QString root = browser->rootPath();
        if (!root.isEmpty())
        {
            return root;
        }
    }
    return QDir::currentPath();
}

EngineUiControlServer::ProfileData ProfileDataService::captureProfileData() const
{
    EngineUiControlServer::ProfileData data;
    data.uiDebug = m_window.uiDebugJson();
    data.uiTree = m_window.uiWidgetTreeJson();
    if (auto* browser = m_window.assetBrowser())
    {
        data.rootPath = browser->rootPath();
    }
    if (auto* viewport = m_window.viewportHost())
    {
        data.sceneItemCount = viewport->sceneItems().size();
        data.hierarchy = m_window.hierarchyJson();
        data.inspector = m_window.inspectorDebugJson();
        for (const auto& item : viewport->sceneProfileJson())
        {
            data.sceneItems.append(item.toObject());
        }
        data.cameraPosition = viewport->cameraPosition();
        data.cameraRotation = viewport->cameraRotation();

        const auto perf = viewport->performanceMetrics();
        data.currentFps = perf.currentFps;
        data.renderIntervalMs = perf.renderIntervalMs;
        data.renderTimerActive = perf.renderTimerActive;
        data.viewportWidth = perf.viewportWidth;
        data.viewportHeight = perf.viewportHeight;
        data.focusedViewportIndex = viewport->focusedViewportIndex();
        data.focusedViewportCameraId = viewport->focusedViewportCameraId();
        data.viewportCameraIds = viewport->viewportCameraIds();
        data.cameraTracking = viewport->cameraTrackingDebugJson();
        data.motionDebugFrame = viewport->motionDebugFrameJson();
        data.motionDebugSummary = viewport->motionDebugSummaryJson();
        data.motionDebugOverlay = viewport->motionDebugOverlayOptionsJson();
        data.tpsState = viewport->thirdPersonShooterStateJson();
    }
    return data;
}

}  // namespace motive::ui
