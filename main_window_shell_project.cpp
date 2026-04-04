#include "main_window_shell.h"
#include "asset_browser_widget.h"
#include "viewport_host_widget.h"

#include <QAction>
#include <QDir>
#include <QFileDialog>
#include <QInputDialog>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QDebug>

namespace motive::ui {

void MainWindowShell::restoreSessionState()
{
    if (!m_assetBrowser)
    {
        return;
    }

    m_restoringSessionState = true;

    const QString restoredRoot = m_projectSession.currentProjectRoot();
    if (!restoredRoot.isEmpty())
    {
        m_assetBrowser->setRootPath(restoredRoot);
    }
    else
    {
        m_assetBrowser->setRootPath(QDir::currentPath());
    }

    m_assetBrowser->restoreGalleryPath(m_projectSession.currentGalleryPath());

    if (m_viewportHost &&
        (!m_projectSession.currentSceneItems().isEmpty() ||
         !m_projectSession.currentViewportAssetPath().isEmpty()))
    {
        if (!m_projectSession.currentSceneItems().isEmpty())
        {
            qDebug() << "[MainWindowShell] Restoring saved scene"
                     << m_projectSession.currentSceneItems().size();
            m_viewportHost->loadSceneFromItems(sceneItemsFromJson(m_projectSession.currentSceneItems()));
        }
        else
        {
            qDebug() << "[MainWindowShell] Restoring legacy single viewport asset"
                     << m_projectSession.currentViewportAssetPath();
            m_viewportHost->loadAssetFromPath(m_projectSession.currentViewportAssetPath());
        }
    }

    // Restore camera state
    if (m_viewportHost) {
        m_viewportHost->setSceneLight(MainWindowShell::sceneLightFromJson(m_projectSession.currentSceneLight()));
        m_viewportHost->setMeshConsolidationEnabled(m_projectSession.currentMeshConsolidationEnabled());
        m_viewportHost->setRenderPath(m_projectSession.currentRenderPath());
        m_viewportHost->setCameraSpeed(m_projectSession.currentCameraSpeed());
        m_viewportHost->setCameraPosition(m_projectSession.currentCameraPosition());
        m_viewportHost->setCameraRotation(m_projectSession.currentCameraRotation());
        updateCameraSettingsPanel();
    }

    m_restoringSessionState = false;

    maybePromptForGltfConversion(m_assetBrowser ? m_assetBrowser->rootPath() : QString());
    refreshHierarchy(m_viewportHost ? m_viewportHost->sceneItems() : QList<ViewportHostWidget::SceneItem>{});
    saveProjectState();
}

void MainWindowShell::setupProjectMenu()
{
    QMenu* projectMenu = menuBar()->addMenu(QStringLiteral("&Project"));

    QAction* newProjectAction = projectMenu->addAction(QStringLiteral("New Project..."));
    connect(newProjectAction, &QAction::triggered, this, [this]() { createProject(); });

    QAction* switchProjectAction = projectMenu->addAction(QStringLiteral("Switch Project..."));
    connect(switchProjectAction, &QAction::triggered, this, [this]() { switchProject(); });
}

void MainWindowShell::refreshWindowTitle()
{
    const QString projectId = m_projectSession.currentProjectId().isEmpty()
        ? QStringLiteral("default")
        : m_projectSession.currentProjectId();
    setWindowTitle(QStringLiteral("Motive Editor [%1]").arg(projectId));
}

void MainWindowShell::createProject()
{
    bool ok = false;
    const QString name = QInputDialog::getText(
        this,
        QStringLiteral("Create Project"),
        QStringLiteral("Project name:"),
        QLineEdit::Normal,
        QString(),
        &ok);
    if (!ok || name.trimmed().isEmpty())
    {
        return;
    }

    const QString rootPath = QFileDialog::getExistingDirectory(
        this,
        QStringLiteral("Select Project Root"),
        m_assetBrowser ? m_assetBrowser->rootPath() : QDir::currentPath(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (!m_projectSession.createProject(name, rootPath))
    {
        QMessageBox::warning(this, QStringLiteral("Create Project"),
                             QStringLiteral("Failed to create project. It may already exist."));
        return;
    }

    if (m_assetBrowser)
    {
        m_assetBrowser->setRootPath(m_projectSession.currentProjectRoot());
        m_assetBrowser->restoreGalleryPath(m_projectSession.currentGalleryPath());
    }
    if (m_viewportHost &&
        (!m_projectSession.currentSceneItems().isEmpty() ||
         !m_projectSession.currentViewportAssetPath().isEmpty()))
    {
        if (!m_projectSession.currentSceneItems().isEmpty())
        {
            m_viewportHost->loadSceneFromItems(sceneItemsFromJson(m_projectSession.currentSceneItems()));
        }
        else
        {
            m_viewportHost->loadAssetFromPath(m_projectSession.currentViewportAssetPath());
        }
    }
    if (m_viewportHost)
    {
        m_viewportHost->setSceneLight(MainWindowShell::sceneLightFromJson(m_projectSession.currentSceneLight()));
    }
    refreshWindowTitle();
}

void MainWindowShell::switchProject()
{
    const QStringList ids = m_projectSession.availableProjectIds();
    if (ids.isEmpty())
    {
        QMessageBox::information(this, QStringLiteral("Switch Project"),
                                 QStringLiteral("No projects are available."));
        return;
    }

    bool ok = false;
    const QString selectedId = QInputDialog::getItem(
        this,
        QStringLiteral("Switch Project"),
        QStringLiteral("Project:"),
        ids,
        qMax(0, ids.indexOf(m_projectSession.currentProjectId())),
        false,
        &ok);
    if (!ok || selectedId.isEmpty())
    {
        return;
    }

    if (!m_projectSession.switchToProject(selectedId))
    {
        QMessageBox::warning(this, QStringLiteral("Switch Project"),
                             QStringLiteral("Failed to switch project."));
        return;
    }

    if (m_assetBrowser)
    {
        m_assetBrowser->setRootPath(m_projectSession.currentProjectRoot());
        m_assetBrowser->restoreGalleryPath(m_projectSession.currentGalleryPath());
    }
    if (m_viewportHost &&
        (!m_projectSession.currentSceneItems().isEmpty() ||
         !m_projectSession.currentViewportAssetPath().isEmpty()))
    {
        if (!m_projectSession.currentSceneItems().isEmpty())
        {
            m_viewportHost->loadSceneFromItems(sceneItemsFromJson(m_projectSession.currentSceneItems()));
        }
        else
        {
            m_viewportHost->loadAssetFromPath(m_projectSession.currentViewportAssetPath());
        }
    }
    if (m_viewportHost)
    {
        m_viewportHost->setSceneLight(MainWindowShell::sceneLightFromJson(m_projectSession.currentSceneLight()));
    }
    refreshWindowTitle();
}

void MainWindowShell::saveUiState()
{
    if (m_restoringSessionState || m_savingUiState)
    {
        return;
    }

    m_savingUiState = true;
    saveProjectState();
    m_projectSession.saveCurrentProject();
    m_savingUiState = false;
}

void MainWindowShell::saveProjectState()
{
    if (m_restoringSessionState)
    {
        return;
    }

    m_projectSession.setCurrentProjectRoot(m_assetBrowser ? m_assetBrowser->rootPath() : QDir::currentPath());
    m_projectSession.setCurrentGalleryPath(m_assetBrowser ? m_assetBrowser->galleryPath() : QString());
    m_projectSession.setCurrentSelectedAssetPath(m_assetBrowser ? m_assetBrowser->selectedAssetPath() : QString());
    m_projectSession.setCurrentViewportAssetPath(m_viewportHost ? m_viewportHost->currentAssetPath() : QString());
    m_projectSession.setCurrentSceneItems(sceneItemsToJson(
        m_viewportHost ? m_viewportHost->sceneItems() : QList<ViewportHostWidget::SceneItem>{}));
    m_projectSession.setCurrentCameraPosition(m_viewportHost ? m_viewportHost->cameraPosition() : QVector3D(0.0f, 0.0f, 3.0f));
    m_projectSession.setCurrentCameraRotation(m_viewportHost ? m_viewportHost->cameraRotation() : QVector3D(0.0f, 0.0f, 0.0f));
    m_projectSession.setCurrentCameraSpeed(m_viewportHost ? m_viewportHost->cameraSpeed() : 0.01f);
    m_projectSession.setCurrentSceneLight(m_viewportHost ? MainWindowShell::sceneLightToJson(m_viewportHost->sceneLight()) : QJsonObject{});
    m_projectSession.setCurrentRenderPath(m_viewportHost ? m_viewportHost->renderPath() : QStringLiteral("forward3d"));
    m_projectSession.setCurrentMeshConsolidationEnabled(m_viewportHost ? m_viewportHost->meshConsolidationEnabled() : true);
    qDebug() << "[MainWindowShell] Saving project state"
             << "projectId=" << m_projectSession.currentProjectId()
             << "root=" << (m_assetBrowser ? m_assetBrowser->rootPath() : QDir::currentPath())
             << "sceneCount=" << (m_viewportHost ? m_viewportHost->sceneItems().size() : 0);
    m_projectSession.saveCurrentProject();
}

}  // namespace motive::ui
