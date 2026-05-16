#include "shell.h"
#include "asset_browser_widget.h"
#include "host_widget.h"
#include "gltf_exporter.h"
#include "physics_interface.h"

#include <QAction>
#include <QDir>
#include <QFileDialog>
#include <QInputDialog>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QDebug>
#include <QThread>

namespace motive::ui {
namespace {

QJsonArray stringListToJsonArray(const QStringList& values)
{
    QJsonArray array;
    for (const QString& value : values)
    {
        array.push_back(value);
    }
    return array;
}

motive::PhysicsEngineType physicsEngineTypeFromSession(const QString& name)
{
    return motive::physicsEngineTypeFromString(name.toStdString());
}

}

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
        m_assetBrowser->setRootPath(m_projectSession.rootDirPath());
    }

    m_assetBrowser->restoreGalleryPath(m_projectSession.currentGalleryPath());

    if (m_viewportHost)
    {
        motive::PhysicsSettings physicsSettings = m_viewportHost->globalPhysicsSettings();
        physicsSettings.engineType = physicsEngineTypeFromSession(m_projectSession.currentPhysicsEngine());
        const QVector3D worldGravity = m_projectSession.currentWorldGravity();
        physicsSettings.gravity = glm::vec3(worldGravity.x(), worldGravity.y(), worldGravity.z());
        physicsSettings.maxSubSteps = m_projectSession.currentPhysicsMaxSubSteps();
        physicsSettings.debugDraw = m_projectSession.currentPhysicsDebugDraw();
        physicsSettings.autoSync = m_projectSession.currentPhysicsAutoSync();
        m_viewportHost->setGlobalPhysicsSettings(physicsSettings);
    }

    if (m_viewportHost &&
        (!m_projectSession.currentSceneItems().isEmpty() ||
         !m_projectSession.currentViewportAssetPath().isEmpty()))
    {
        if (!m_projectSession.currentSceneItems().isEmpty())
        {
            qDebug() << "[MainWindowShell] Restoring saved scene"
                     << m_projectSession.currentSceneItems().size();
            m_viewportHost->loadSceneFromItems(sceneItemsFromJson(m_projectSession.currentSceneItems()));
            
            // Restore camera configs after scene is loaded
            if (!m_projectSession.currentCameraConfigs().isEmpty())
            {
                qDebug() << "[MainWindowShell] Restoring camera configs"
                         << m_projectSession.currentCameraConfigs().size();
                m_viewportHost->setCameraConfigs(cameraConfigsFromJson(m_projectSession.currentCameraConfigs()));
            }
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
        ViewportHostWidget::ViewportLayout savedLayout = m_viewportHost->viewportLayout();
        savedLayout.count = m_projectSession.currentViewportCount();
        const QJsonArray savedCameraIds = m_projectSession.currentViewportCameraIds();
        savedLayout.cameraIds.clear();
        for (const QJsonValue& value : savedCameraIds)
        {
            savedLayout.cameraIds.push_back(value.toString());
        }
        m_viewportHost->setViewportLayout(savedLayout);
        m_viewportHost->setSceneLight(MainWindowShell::sceneLightFromJson(m_projectSession.currentSceneLight()));
        m_viewportHost->setMeshConsolidationEnabled(m_projectSession.currentMeshConsolidationEnabled());
        m_viewportHost->setRenderPath(m_projectSession.currentRenderPath());
        m_viewportHost->setCameraSpeed(m_projectSession.currentCameraSpeed());
        m_viewportHost->setCameraPosition(m_projectSession.currentCameraPosition());
        m_viewportHost->setCameraRotation(m_projectSession.currentCameraRotation());
        updateCameraSettingsPanel();
    }
    applyUiState(m_projectSession.currentUiState());

    m_restoringSessionState = false;

    maybePromptForGltfConversion(m_assetBrowser ? m_assetBrowser->rootPath() : QString());
    refreshHierarchy(m_viewportHost ? m_viewportHost->sceneItems() : QList<ViewportHostWidget::SceneItem>{});
    saveProjectState();
}

void MainWindowShell::setupProjectMenu()
{
    QMenu* projectMenu = menuBar()->addMenu(QStringLiteral("&Project"));

    QAction* mediaDirProjectsAction = projectMenu->addAction(QStringLiteral("Media Dir \"Projects\""));
    connect(mediaDirProjectsAction, &QAction::triggered, this, [this]() { setMediaDirProjects(); });
    projectMenu->addSeparator();

    QAction* newProjectAction = projectMenu->addAction(QStringLiteral("New Project..."));
    connect(newProjectAction, &QAction::triggered, this, [this]() { createProject(); });

    QAction* switchProjectAction = projectMenu->addAction(QStringLiteral("Switch Project..."));
    connect(switchProjectAction, &QAction::triggered, this, [this]() { switchProject(); });

    projectMenu->addSeparator();
    QAction* exportSelectedAction = projectMenu->addAction(QStringLiteral("Export Selected Scene Item as GLB/GLTF..."));
    connect(exportSelectedAction, &QAction::triggered, this, [this]() { exportSelectedSceneItemToGltf(); });
}

void MainWindowShell::setMediaDirProjects()
{
    const QString rootPath = m_projectSession.rootDirPath();
    if (rootPath.isEmpty() || !QDir(rootPath).exists())
    {
        QMessageBox::warning(this, QStringLiteral("Media Dir \"Projects\""),
                             QStringLiteral("Projects media directory is not available."));
        return;
    }

    if (m_assetBrowser)
    {
        m_assetBrowser->setRootPath(rootPath);
        m_assetBrowser->restoreGalleryPath(QString());
    }
    m_projectSession.setCurrentProjectRoot(rootPath);
    saveProjectState();
}

int MainWindowShell::selectedSceneItemIndex() const
{
    if (!m_hierarchyTree)
    {
        return -1;
    }
    QTreeWidgetItem* current = m_hierarchyTree->currentItem();
    if (!current)
    {
        return -1;
    }
    const int nodeType = current->data(0, Qt::UserRole + 3).toInt();
    if (nodeType != static_cast<int>(ViewportHostWidget::HierarchyNode::Type::SceneItem))
    {
        return -1;
    }
    return current->data(0, Qt::UserRole).toInt();
}

void MainWindowShell::exportSelectedSceneItemToGltf()
{
    const int sceneIndex = selectedSceneItemIndex();
    if (sceneIndex < 0 || sceneIndex >= m_sceneItems.size())
    {
        QMessageBox::information(this,
                                 QStringLiteral("Export Scene Item"),
                                 QStringLiteral("Select a scene item in the hierarchy first."));
        return;
    }

    const ViewportHostWidget::SceneItem& item = m_sceneItems[sceneIndex];
    const QFileInfo sourceInfo(item.sourcePath);
    if (!sourceInfo.exists() ||
        sourceInfo.suffix().compare(QStringLiteral("fbx"), Qt::CaseInsensitive) != 0)
    {
        QMessageBox::warning(this,
                             QStringLiteral("Export Scene Item"),
                             QStringLiteral("The selected scene item must come from an FBX source asset."));
        return;
    }

    const QString defaultPath = sourceInfo.absolutePath() + QDir::separator() +
                                sourceInfo.completeBaseName() + QStringLiteral(".glb");
    const QString outputPath = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Export GLB/GLTF"),
        defaultPath,
        QStringLiteral("glTF Binary (*.glb);;glTF JSON (*.gltf)"));
    if (outputPath.isEmpty())
    {
        return;
    }

    QString errorMessage;
    if (!motive::exporter::exportFbxAssetToGltf(item.sourcePath, outputPath, &errorMessage))
    {
        QMessageBox::warning(this,
                             QStringLiteral("Export GLB/GLTF"),
                             errorMessage.isEmpty() ? QStringLiteral("Export failed.") : errorMessage);
        return;
    }

    QMessageBox::information(this,
                             QStringLiteral("Export GLB/GLTF"),
                             QStringLiteral("Exported %1").arg(QDir::toNativeSeparators(outputPath)));
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
        m_assetBrowser ? m_assetBrowser->rootPath() : m_projectSession.rootDirPath(),
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
            if (!m_projectSession.currentCameraConfigs().isEmpty())
            {
                m_viewportHost->setCameraConfigs(cameraConfigsFromJson(m_projectSession.currentCameraConfigs()));
            }
        }
        else
        {
            m_viewportHost->loadAssetFromPath(m_projectSession.currentViewportAssetPath());
        }
    }
    if (m_viewportHost)
    {
        ViewportHostWidget::ViewportLayout savedLayout = m_viewportHost->viewportLayout();
        savedLayout.count = m_projectSession.currentViewportCount();
        const QJsonArray savedCameraIds = m_projectSession.currentViewportCameraIds();
        savedLayout.cameraIds.clear();
        for (const QJsonValue& value : savedCameraIds)
        {
            savedLayout.cameraIds.push_back(value.toString());
        }
        m_viewportHost->setViewportLayout(savedLayout);
        m_viewportHost->setSceneLight(MainWindowShell::sceneLightFromJson(m_projectSession.currentSceneLight()));
    }
    applyUiState(m_projectSession.currentUiState());
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
            if (!m_projectSession.currentCameraConfigs().isEmpty())
            {
                m_viewportHost->setCameraConfigs(cameraConfigsFromJson(m_projectSession.currentCameraConfigs()));
            }
        }
        else
        {
            m_viewportHost->loadAssetFromPath(m_projectSession.currentViewportAssetPath());
        }
    }
    if (m_viewportHost)
    {
        ViewportHostWidget::ViewportLayout savedLayout = m_viewportHost->viewportLayout();
        savedLayout.count = m_projectSession.currentViewportCount();
        const QJsonArray savedCameraIds = m_projectSession.currentViewportCameraIds();
        savedLayout.cameraIds.clear();
        for (const QJsonValue& value : savedCameraIds)
        {
            savedLayout.cameraIds.push_back(value.toString());
        }
        m_viewportHost->setViewportLayout(savedLayout);
        m_viewportHost->setSceneLight(MainWindowShell::sceneLightFromJson(m_projectSession.currentSceneLight()));
    }
    applyUiState(m_projectSession.currentUiState());
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

void MainWindowShell::requestProjectStateSave()
{
    const auto flushSave = [this]()
    {
        performProjectStateSave();
    };

    if (QThread::currentThread() == thread())
    {
        flushSave();
        return;
    }

    QMetaObject::invokeMethod(
        this,
        flushSave,
        Qt::BlockingQueuedConnection);
}

void MainWindowShell::saveProjectState()
{
    if (m_restoringSessionState)
    {
        return;
    }

    if (!m_projectSaveTimer)
    {
        m_projectSaveTimer = new QTimer(this);
        m_projectSaveTimer->setSingleShot(true);
        connect(m_projectSaveTimer, &QTimer::timeout, this, [this]()
        {
            performProjectStateSave();
        });
    }

    m_projectSaveQueued = true;
    m_projectSaveTimer->start(900);
}

void MainWindowShell::flushPendingProjectStateSave()
{
    if (!m_projectSaveQueued)
    {
        return;
    }

    if (m_projectSaveTimer)
    {
        m_projectSaveTimer->stop();
    }
    performProjectStateSave();
}

void MainWindowShell::performProjectStateSave()
{
    if (m_restoringSessionState)
    {
        return;
    }

    m_projectSaveQueued = false;

    m_projectSession.setCurrentProjectRoot(m_assetBrowser ? m_assetBrowser->rootPath() : QDir::currentPath());
    m_projectSession.setCurrentGalleryPath(m_assetBrowser ? m_assetBrowser->galleryPath() : QString());
    m_projectSession.setCurrentSelectedAssetPath(m_assetBrowser ? m_assetBrowser->selectedAssetPath() : QString());
    m_projectSession.setCurrentViewportAssetPath(m_viewportHost ? m_viewportHost->currentAssetPath() : QString());
    const QJsonArray previousSceneItems = m_projectSession.currentSceneItems();
    const QJsonArray currentSceneItems = sceneItemsToJson(
        m_viewportHost ? m_viewportHost->sceneItems() : QList<ViewportHostWidget::SceneItem>{});
    if (!currentSceneItems.isEmpty() || previousSceneItems.isEmpty())
    {
        m_projectSession.setCurrentSceneItems(currentSceneItems);
    }
    m_projectSession.setCurrentCameraConfigs(cameraConfigsToJson(
        m_viewportHost ? m_viewportHost->cameraConfigs() : QList<ViewportHostWidget::CameraConfig>{}));
    m_projectSession.setCurrentCameraPosition(m_viewportHost ? m_viewportHost->cameraPosition() : QVector3D(0.0f, 0.0f, 3.0f));
    m_projectSession.setCurrentCameraRotation(m_viewportHost ? m_viewportHost->cameraRotation() : QVector3D(0.0f, 0.0f, 0.0f));
    m_projectSession.setCurrentCameraSpeed(m_viewportHost ? m_viewportHost->cameraSpeed() : 0.01f);
    m_projectSession.setCurrentFreeFlyCameraEnabled(m_viewportHost ? m_viewportHost->isFreeFlyCameraEnabled() : true);
    m_projectSession.setCurrentSceneLight(m_viewportHost ? MainWindowShell::sceneLightToJson(m_viewportHost->sceneLight()) : QJsonObject{});
    m_projectSession.setCurrentRenderPath(m_viewportHost ? m_viewportHost->renderPath() : QStringLiteral("forward3d"));
    m_projectSession.setCurrentMeshConsolidationEnabled(m_viewportHost ? m_viewportHost->meshConsolidationEnabled() : true);
    if (m_viewportHost)
    {
        const motive::PhysicsSettings physicsSettings = m_viewportHost->globalPhysicsSettings();
        m_projectSession.setCurrentPhysicsEngine(QString::fromUtf8(motive::physicsEngineTypeToString(physicsSettings.engineType)));
        m_projectSession.setCurrentWorldGravity(QVector3D(
            physicsSettings.gravity.x,
            physicsSettings.gravity.y,
            physicsSettings.gravity.z));
        m_projectSession.setCurrentPhysicsMaxSubSteps(physicsSettings.maxSubSteps);
        m_projectSession.setCurrentPhysicsDebugDraw(physicsSettings.debugDraw);
        m_projectSession.setCurrentPhysicsAutoSync(physicsSettings.autoSync);
    }
    if (m_viewportHost)
    {
        const auto layout = m_viewportHost->viewportLayout();
        m_projectSession.setCurrentViewportCount(layout.count);
        m_projectSession.setCurrentViewportCameraIds(stringListToJsonArray(layout.cameraIds));
    }
    else
    {
        m_projectSession.setCurrentViewportCount(1);
        m_projectSession.setCurrentViewportCameraIds(QJsonArray{});
    }
    m_projectSession.setCurrentUiState(captureUiState());
    qDebug() << "[MainWindowShell] Saving project state"
             << "projectId=" << m_projectSession.currentProjectId()
             << "root=" << (m_assetBrowser ? m_assetBrowser->rootPath() : QDir::currentPath())
             << "sceneCount=" << (m_viewportHost ? m_viewportHost->sceneItems().size() : 0);
    m_projectSession.saveCurrentProject();
}

}  // namespace motive::ui
