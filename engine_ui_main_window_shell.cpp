#include "engine_ui_main_window_shell.h"
#include "engine_ui_asset_browser_widget.h"
#include "engine_ui_viewport_host_widget.h"

#include <QAction>
#include <QDockWidget>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QListWidget>
#include <QSplitter>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>
#include <QColorDialog>
#include <QTabWidget>
#include <QDebug>

namespace motive::ui {

namespace {

struct ConversionCommand
{
    QString program;
    QStringList arguments;
};

QString formatCommandLine(const ConversionCommand& command)
{
    QStringList parts;
    parts.push_back(command.program);
    parts.append(command.arguments);

    for (QString& part : parts)
    {
        if (part.contains(' '))
        {
            part = QStringLiteral("\"%1\"").arg(part);
        }
    }

    return parts.join(' ');
}

QString replacePlaceholder(QString value, const QString& inputPath, const QString& outputPath)
{
    value.replace(QStringLiteral("{input}"), inputPath);
    value.replace(QStringLiteral("{output}"), outputPath);
    return value;
}

bool isFbxFile(const QFileInfo& inputFile)
{
    return inputFile.suffix().compare(QStringLiteral("fbx"), Qt::CaseInsensitive) == 0;
}

QString suggestedConverterInstallText()
{
#if defined(Q_OS_MACOS)
    return QStringLiteral(
        "Suggested tools:\n"
        "- brew install assimp\n"
        "- or install FBX2glTF / fbx2gltf separately\n"
        "- or set MOTIVE_GLTF_CONVERTER='your-command {input} {output}'");
#elif defined(Q_OS_WIN)
    return QStringLiteral(
        "Suggested tools:\n"
        "- install FBX2glTF or fbx2gltf and add it to PATH\n"
        "- or install assimp and use `assimp export`\n"
        "- or set MOTIVE_GLTF_CONVERTER=\"your-command {input} {output}\"");
#elif defined(Q_OS_LINUX)
    QString distroId;
    QFile osRelease(QStringLiteral("/etc/os-release"));
    if (osRelease.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        const QString text = QString::fromUtf8(osRelease.readAll());
        const QStringList lines = text.split('\n');
        for (const QString& line : lines)
        {
            if (line.startsWith(QStringLiteral("ID=")))
            {
                distroId = line.mid(3).trimmed().remove('"').toLower();
                break;
            }
        }
    }

    if (distroId == QStringLiteral("ubuntu") || distroId == QStringLiteral("debian"))
    {
        return QStringLiteral(
            "Suggested tools:\n"
            "- sudo apt install assimp-utils\n"
            "- or install FBX2glTF / fbx2gltf separately\n"
            "- or set MOTIVE_GLTF_CONVERTER='your-command {input} {output}'");
    }
    if (distroId == QStringLiteral("fedora"))
    {
        return QStringLiteral(
            "Suggested tools:\n"
            "- sudo dnf install assimp\n"
            "- or install FBX2glTF / fbx2gltf separately\n"
            "- or set MOTIVE_GLTF_CONVERTER='your-command {input} {output}'");
    }
    if (distroId == QStringLiteral("arch"))
    {
        return QStringLiteral(
            "Suggested tools:\n"
            "- sudo pacman -S assimp\n"
            "- or install FBX2glTF / fbx2gltf separately\n"
            "- or set MOTIVE_GLTF_CONVERTER='your-command {input} {output}'");
    }

    return QStringLiteral(
        "Suggested tools:\n"
        "- install assimp, FBX2glTF, or fbx2gltf\n"
        "- or set MOTIVE_GLTF_CONVERTER='your-command {input} {output}'");
#else
    return QStringLiteral(
        "Suggested tools:\n"
        "- install assimp, FBX2glTF, or fbx2gltf\n"
        "- or set MOTIVE_GLTF_CONVERTER='your-command {input} {output}'");
#endif
}

std::optional<ConversionCommand> conversionCommandForFile(const QFileInfo& inputFile)
{
    const QString inputPath = inputFile.absoluteFilePath();
    const QString outputPath = inputFile.absolutePath() + QDir::separator() +
                               inputFile.completeBaseName() + QStringLiteral(".gltf");
    const bool isFbx = isFbxFile(inputFile);

    const QString overrideCommand = qEnvironmentVariable("MOTIVE_GLTF_CONVERTER");
    if (!overrideCommand.trimmed().isEmpty())
    {
        QStringList parts = QProcess::splitCommand(overrideCommand);
        if (!parts.isEmpty())
        {
            ConversionCommand command;
            command.program = replacePlaceholder(parts.takeFirst(), inputPath, outputPath);
            for (QString& part : parts)
            {
                command.arguments.push_back(replacePlaceholder(part, inputPath, outputPath));
            }
            return command;
        }
    }

    const QString fbx2gltf = QStandardPaths::findExecutable(QStringLiteral("fbx2gltf"));
    if (isFbx && !fbx2gltf.isEmpty())
    {
        return ConversionCommand{fbx2gltf, {QStringLiteral("-i"), inputPath, QStringLiteral("-o"), outputPath}};
    }

    const QString FBX2glTF = QStandardPaths::findExecutable(QStringLiteral("FBX2glTF"));
    if (isFbx && !FBX2glTF.isEmpty())
    {
        return ConversionCommand{FBX2glTF, {QStringLiteral("-i"), inputPath, QStringLiteral("-o"), outputPath}};
    }

    if (isFbx)
    {
        return std::nullopt;
    }

    const QString assimp = QStandardPaths::findExecutable(QStringLiteral("assimp"));
    if (!assimp.isEmpty())
    {
        return ConversionCommand{assimp, {QStringLiteral("export"), inputPath, outputPath}};
    }

    return std::nullopt;
}

QString missingConverterMessageForFile(const QFileInfo& inputFile)
{
    if (isFbxFile(inputFile))
    {
        return QStringLiteral(
            "No animation-safe FBX converter detected.\n"
            "FBX files are not routed through `assimp export` because animation, skinning, or rig data may be lost.\n\n%1")
            .arg(suggestedConverterInstallText());
    }

    return QStringLiteral(
        "No converter found.\n%1").arg(suggestedConverterInstallText());
}

bool convertSourceFileToGltf(const QFileInfo& inputFile, QString* errorMessage)
{
    const auto command = conversionCommandForFile(inputFile);
    if (!command.has_value())
    {
        if (errorMessage)
        {
            *errorMessage = missingConverterMessageForFile(inputFile);
        }
        return false;
    }

    QProcess process;
    process.start(command->program, command->arguments);
    if (!process.waitForStarted())
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("Failed to start converter: %1").arg(command->program);
        }
        return false;
    }

    process.closeWriteChannel();
    if (!process.waitForFinished(-1))
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("Converter timed out for %1").arg(inputFile.fileName());
        }
        return false;
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
    {
        if (errorMessage)
        {
            const QString stderrText = QString::fromUtf8(process.readAllStandardError()).trimmed();
            const QString stdoutText = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
            *errorMessage = !stderrText.isEmpty() ? stderrText : (!stdoutText.isEmpty() ? stdoutText : QStringLiteral("converter failed"));
        }
        return false;
    }

    const QString expectedGltf = inputFile.absolutePath() + QDir::separator() +
                                 inputFile.completeBaseName() + QStringLiteral(".gltf");
    if (!QFileInfo::exists(expectedGltf))
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("Converter completed but did not create %1").arg(QFileInfo(expectedGltf).fileName());
        }
        return false;
    }

    return true;
}

QList<QFileInfo> findConvertibleSourcesWithoutGltf(const QString& rootPath, const QStringList& convertibleSuffixes)
{
    QList<QFileInfo> results;
    QDirIterator it(rootPath,
                    QDir::Files | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        it.next();
        const QFileInfo entry = it.fileInfo();
        const QString suffix = entry.suffix().toLower();
        if (!convertibleSuffixes.contains(suffix))
        {
            continue;
        }

        const QString expectedGltf = entry.absolutePath() + QDir::separator() +
                                     entry.completeBaseName() + QStringLiteral(".gltf");
        if (!QFileInfo::exists(expectedGltf))
        {
            results.push_back(entry);
        }
    }
    return results;
}

}  // namespace

MainWindowShell::MainWindowShell(QWidget* parent)
    : QMainWindow(parent)
{
    resize(1600, 900);

    m_splitter = new QSplitter(this);
    m_leftPane = new QWidget(m_splitter);
    auto* leftLayout = new QVBoxLayout(m_leftPane);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(8);

    m_assetBrowser = new AssetBrowserWidget(m_leftPane);
    leftLayout->addWidget(m_assetBrowser, 1);
    leftLayout->addWidget(new QLabel(QStringLiteral("Hierarchy"), m_leftPane));
    m_hierarchyList = new QListWidget(m_leftPane);
    m_hierarchyList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_hierarchyList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_hierarchyList, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos)
    {
        const int row = m_hierarchyList->indexAt(pos).row();
        if (row < 0 || row >= m_sceneItems.size() || !m_viewportHost)
        {
            return;
        }
        QMenu menu(this);
        QAction* relocateAction = menu.addAction(QStringLiteral("Relocate in front of camera"));
        if (menu.exec(m_hierarchyList->mapToGlobal(pos)) == relocateAction)
        {
            m_viewportHost->relocateSceneItemInFrontOfCamera(row);
            updateInspectorForSelection(row);
        }
    });
    leftLayout->addWidget(m_hierarchyList, 0);

    m_viewportHost = new ViewportHostWidget(m_splitter);
    m_assetBrowser->setPreviewAnchorWidget(m_viewportHost);
    m_assetBrowser->setActivationCallback([this](const AssetBrowserSelection& selection)
    {
        if (!selection.isDirectory && m_viewportHost)
        {
            m_viewportHost->loadAssetFromPath(selection.filePath);
            m_projectSession.setCurrentSelectedAssetPath(selection.filePath);
            m_projectSession.setCurrentViewportAssetPath(m_viewportHost->currentAssetPath());
            saveProjectState();
        }
    });
    m_viewportHost->setSceneChangedCallback([this](const QList<ViewportHostWidget::SceneItem>& items)
    {
        refreshHierarchy(items);
        saveProjectState();
    });
    m_viewportHost->setCameraChangedCallback([this]()
    {
        updateCameraSettingsPanel();
        saveProjectState();
    });
    m_assetBrowser->setRootPathChangedCallback([this](const QString& rootPath)
    {
        m_projectSession.setCurrentProjectRoot(rootPath);
        m_projectSession.setCurrentGalleryPath(m_assetBrowser ? m_assetBrowser->galleryPath() : QString());
        saveProjectState();
        refreshWindowTitle();
        maybePromptForGltfConversion(rootPath);
    });
    m_splitter->addWidget(m_leftPane);
    m_splitter->addWidget(m_viewportHost);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    setCentralWidget(m_splitter);

    auto* inspectorDock = new QDockWidget(QStringLiteral("Inspector"), this);
    inspectorDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_rightTabs = new QTabWidget(inspectorDock);
    auto* inspectorPanel = new QWidget(m_rightTabs);
    auto* inspectorLayout = new QFormLayout(inspectorPanel);
    m_inspectorNameValue = new QLabel(QStringLiteral("-"), inspectorPanel);
    m_inspectorPathValue = new QLabel(QStringLiteral("-"), inspectorPanel);
    m_inspectorPathValue->setWordWrap(true);

    // Create translation spin boxes
    auto* translationWidget = new QWidget(inspectorPanel);
    auto* translationLayout = new QHBoxLayout(translationWidget);
    m_inspectorTranslationX = createSpinBox(inspectorPanel, -1000.0, 1000.0, 0.001);
    m_inspectorTranslationY = createSpinBox(inspectorPanel, -1000.0, 1000.0, 0.001);
    m_inspectorTranslationZ = createSpinBox(inspectorPanel, -1000.0, 1000.0, 0.001);
    translationLayout->addWidget(new QLabel("X:", translationWidget));
    translationLayout->addWidget(m_inspectorTranslationX);
    translationLayout->addWidget(new QLabel("Y:", translationWidget));
    translationLayout->addWidget(m_inspectorTranslationY);
    translationLayout->addWidget(new QLabel("Z:", translationWidget));
    translationLayout->addWidget(m_inspectorTranslationZ);
    translationLayout->setContentsMargins(0, 0, 0, 0);

    // Create rotation spin boxes
    auto* rotationWidget = new QWidget(inspectorPanel);
    auto* rotationLayout = new QHBoxLayout(rotationWidget);
    m_inspectorRotationX = createSpinBox(inspectorPanel, -360.0, 360.0, 0.1);
    m_inspectorRotationY = createSpinBox(inspectorPanel, -360.0, 360.0, 0.1);
    m_inspectorRotationZ = createSpinBox(inspectorPanel, -360.0, 360.0, 0.1);
    rotationLayout->addWidget(new QLabel("X:", rotationWidget));
    rotationLayout->addWidget(m_inspectorRotationX);
    rotationLayout->addWidget(new QLabel("Y:", rotationWidget));
    rotationLayout->addWidget(m_inspectorRotationY);
    rotationLayout->addWidget(new QLabel("Z:", rotationWidget));
    rotationLayout->addWidget(m_inspectorRotationZ);
    rotationLayout->setContentsMargins(0, 0, 0, 0);

    // Create scale spin boxes
    auto* scaleWidget = new QWidget(inspectorPanel);
    auto* scaleLayout = new QHBoxLayout(scaleWidget);
    m_inspectorScaleX = createSpinBox(inspectorPanel, 0.001, 1000.0, 0.001);
    m_inspectorScaleY = createSpinBox(inspectorPanel, 0.001, 1000.0, 0.001);
    m_inspectorScaleZ = createSpinBox(inspectorPanel, 0.001, 1000.0, 0.001);
    scaleLayout->addWidget(new QLabel("X:", scaleWidget));
    scaleLayout->addWidget(m_inspectorScaleX);
    scaleLayout->addWidget(new QLabel("Y:", scaleWidget));
    scaleLayout->addWidget(m_inspectorScaleY);
    scaleLayout->addWidget(new QLabel("Z:", scaleWidget));
    scaleLayout->addWidget(m_inspectorScaleZ);
    scaleLayout->setContentsMargins(0, 0, 0, 0);

    inspectorLayout->addRow(QStringLiteral("Name"), m_inspectorNameValue);
    inspectorLayout->addRow(QStringLiteral("Source"), m_inspectorPathValue);
    inspectorLayout->addRow(QStringLiteral("Translation"), translationWidget);
    inspectorLayout->addRow(QStringLiteral("Rotation"), rotationWidget);
    inspectorLayout->addRow(QStringLiteral("Scale"), scaleWidget);
    m_rightTabs->addTab(inspectorPanel, QStringLiteral("Element"));
    inspectorDock->setWidget(m_rightTabs);
    addDockWidget(Qt::RightDockWidgetArea, inspectorDock);

    setupCameraSettingsPanel();

    connect(m_hierarchyList, &QListWidget::currentRowChanged, this, [this](int row)
    {
        updateInspectorForSelection(row);
    });

    // Connect spin box value changes to update scene items
    auto updateTransform = [this]() {
        if (m_updatingInspector) return;
        const int row = m_hierarchyList->currentRow();
        if (row >= 0 && row < m_sceneItems.size()) {
            QVector3D translation(
                m_inspectorTranslationX->value(),
                m_inspectorTranslationY->value(),
                m_inspectorTranslationZ->value()
            );
            QVector3D rotation(
                m_inspectorRotationX->value(),
                m_inspectorRotationY->value(),
                m_inspectorRotationZ->value()
            );
            QVector3D scale(
                m_inspectorScaleX->value(),
                m_inspectorScaleY->value(),
                m_inspectorScaleZ->value()
            );
            m_viewportHost->updateSceneItemTransform(row, translation, rotation, scale);
            m_sceneItems[row].translation = translation;
            m_sceneItems[row].rotation = rotation;
            m_sceneItems[row].scale = scale;
            saveProjectState();
        }
    };

    connect(m_inspectorTranslationX, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, updateTransform);
    connect(m_inspectorTranslationY, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, updateTransform);
    connect(m_inspectorTranslationZ, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, updateTransform);
    connect(m_inspectorRotationX, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, updateTransform);
    connect(m_inspectorRotationY, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, updateTransform);
    connect(m_inspectorRotationZ, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, updateTransform);
    connect(m_inspectorScaleX, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, updateTransform);
    connect(m_inspectorScaleY, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, updateTransform);
    connect(m_inspectorScaleZ, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, updateTransform);

    setupProjectMenu();
    restoreSessionState();
    refreshWindowTitle();
    maybePromptForGltfConversion(m_assetBrowser ? m_assetBrowser->rootPath() : m_projectSession.currentProjectRoot());

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

void MainWindowShell::closeEvent(QCloseEvent* event)
{
    saveUiState();
    QMainWindow::closeEvent(event);
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
        m_viewportHost->setRenderPath(m_projectSession.currentRenderPath());
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
    m_projectSession.setCurrentRenderPath(m_viewportHost ? m_viewportHost->renderPath() : QStringLiteral("forward3d"));
    qDebug() << "[MainWindowShell] Saving project state"
             << "projectId=" << m_projectSession.currentProjectId()
             << "root=" << (m_assetBrowser ? m_assetBrowser->rootPath() : QDir::currentPath())
             << "sceneCount=" << (m_viewportHost ? m_viewportHost->sceneItems().size() : 0);
    m_projectSession.saveCurrentProject();
}

void MainWindowShell::refreshHierarchy(const QList<ViewportHostWidget::SceneItem>& items)
{
    if (!m_hierarchyList)
    {
        return;
    }

    m_sceneItems = items;
    const int previousRow = m_hierarchyList->currentRow();
    m_hierarchyList->clear();
    for (int i = 0; i < items.size(); ++i)
    {
        const auto& item = items[i];
        auto* container = new QWidget(m_hierarchyList);
        auto* layout = new QHBoxLayout(container);
        layout->setContentsMargins(4, 2, 4, 2);
        layout->setSpacing(6);

        auto* eyeButton = new QPushButton(container);
        eyeButton->setFlat(true);
        eyeButton->setFixedSize(22, 22);
        eyeButton->setText(item.visible ? QString::fromUtf8("\xF0\x9F\x91\x81") : QString::fromUtf8("\xF0\x9F\x9A\xAB"));
        eyeButton->setProperty("row", i);
        connect(eyeButton, &QPushButton::clicked, this, [this, eyeButton]()
        {
            const int row = eyeButton->property("row").toInt();
            if (row < 0 || row >= m_sceneItems.size() || !m_viewportHost)
            {
                return;
            }
            const bool willBeVisible = !m_sceneItems[row].visible;
            m_sceneItems[row].visible = willBeVisible;
            m_viewportHost->setSceneItemVisible(row, willBeVisible);
            eyeButton->setText(willBeVisible ? QString::fromUtf8("\xF0\x9F\x91\x81") : QString::fromUtf8("\xF0\x9F\x9A\xAB"));
        });

        auto* nameLabel = new QLabel(item.name, container);
        nameLabel->setToolTip(QDir::toNativeSeparators(item.sourcePath));
        layout->addWidget(eyeButton);
        layout->addWidget(nameLabel, 1);

        auto* rowItem = new QListWidgetItem(m_hierarchyList);
        rowItem->setSizeHint(container->sizeHint());
        m_hierarchyList->addItem(rowItem);
        m_hierarchyList->setItemWidget(rowItem, container);
    }
    if (!items.isEmpty())
    {
        const int lastRow = static_cast<int>(items.size()) - 1;
        m_hierarchyList->setCurrentRow(std::clamp(previousRow, 0, lastRow));
    }
    else
    {
        updateInspectorForSelection(-1);
    }
}

void MainWindowShell::updateInspectorForSelection(int row)
{
    m_updatingInspector = true;
    const bool valid = row >= 0 && row < m_sceneItems.size();
    const auto setValue = [](QLabel* label, const QString& value)
    {
        if (label)
        {
            label->setText(value);
        }
    };

    if (!valid)
    {
        setValue(m_inspectorNameValue, QStringLiteral("-"));
        setValue(m_inspectorPathValue, QStringLiteral("-"));
        if (m_inspectorTranslationX) m_inspectorTranslationX->setValue(0.0);
        if (m_inspectorTranslationY) m_inspectorTranslationY->setValue(0.0);
        if (m_inspectorTranslationZ) m_inspectorTranslationZ->setValue(0.0);
        if (m_inspectorRotationX) m_inspectorRotationX->setValue(0.0);
        if (m_inspectorRotationY) m_inspectorRotationY->setValue(0.0);
        if (m_inspectorRotationZ) m_inspectorRotationZ->setValue(0.0);
        if (m_inspectorScaleX) m_inspectorScaleX->setValue(1.0);
        if (m_inspectorScaleY) m_inspectorScaleY->setValue(1.0);
        if (m_inspectorScaleZ) m_inspectorScaleZ->setValue(1.0);
        m_updatingInspector = false;
        return;
    }

    const auto& item = m_sceneItems[row];
    setValue(m_inspectorNameValue, item.name);
    setValue(m_inspectorPathValue, QDir::toNativeSeparators(item.sourcePath));
    if (m_inspectorTranslationX) m_inspectorTranslationX->setValue(item.translation.x());
    if (m_inspectorTranslationY) m_inspectorTranslationY->setValue(item.translation.y());
    if (m_inspectorTranslationZ) m_inspectorTranslationZ->setValue(item.translation.z());
    if (m_inspectorRotationX) m_inspectorRotationX->setValue(item.rotation.x());
    if (m_inspectorRotationY) m_inspectorRotationY->setValue(item.rotation.y());
    if (m_inspectorRotationZ) m_inspectorRotationZ->setValue(item.rotation.z());
    if (m_inspectorScaleX) m_inspectorScaleX->setValue(item.scale.x());
    if (m_inspectorScaleY) m_inspectorScaleY->setValue(item.scale.y());
    if (m_inspectorScaleZ) m_inspectorScaleZ->setValue(item.scale.z());
    m_updatingInspector = false;
}

QJsonArray MainWindowShell::sceneItemsToJson(const QList<ViewportHostWidget::SceneItem>& items) const
{
    QJsonArray array;
    for (const auto& item : items)
    {
        array.push_back(QJsonObject{
            {QStringLiteral("name"), item.name},
            {QStringLiteral("sourcePath"), item.sourcePath},
            {QStringLiteral("translation"), QJsonArray{item.translation.x(), item.translation.y(), item.translation.z()}},
            {QStringLiteral("rotation"), QJsonArray{item.rotation.x(), item.rotation.y(), item.rotation.z()}},
            {QStringLiteral("scale"), QJsonArray{item.scale.x(), item.scale.y(), item.scale.z()}},
            {QStringLiteral("visible"), item.visible}
        });
    }
    return array;
}

QList<ViewportHostWidget::SceneItem> MainWindowShell::sceneItemsFromJson(const QJsonArray& items) const
{
    auto readVector = [](const QJsonValue& value, const QVector3D& fallback)
    {
        const QJsonArray array = value.toArray();
        if (array.size() != 3)
        {
            return fallback;
        }
        return QVector3D(static_cast<float>(array.at(0).toDouble(fallback.x())),
                         static_cast<float>(array.at(1).toDouble(fallback.y())),
                         static_cast<float>(array.at(2).toDouble(fallback.z())));
    };

    QList<ViewportHostWidget::SceneItem> result;
    for (const QJsonValue& value : items)
    {
        const QJsonObject object = value.toObject();
        const QString sourcePath = object.value(QStringLiteral("sourcePath")).toString();
        if (sourcePath.isEmpty())
        {
            continue;
        }
        result.push_back(ViewportHostWidget::SceneItem{
            object.value(QStringLiteral("name")).toString(QFileInfo(sourcePath).completeBaseName()),
            sourcePath,
            readVector(object.value(QStringLiteral("translation")), QVector3D(0.0f, 0.0f, 0.0f)),
            readVector(object.value(QStringLiteral("rotation")), QVector3D(-90.0f, 0.0f, 0.0f)),
            readVector(object.value(QStringLiteral("scale")), QVector3D(1.0f, 1.0f, 1.0f)),
            object.value(QStringLiteral("visible")).toBool(true)
        });
    }
    return result;
}

QString MainWindowShell::vectorText(const QVector3D& value) const
{
    return QStringLiteral("%1, %2, %3")
        .arg(QString::number(value.x(), 'f', 3))
        .arg(QString::number(value.y(), 'f', 3))
        .arg(QString::number(value.z(), 'f', 3));
}

QDoubleSpinBox* MainWindowShell::createSpinBox(QWidget* parent, double min, double max, double step)
{
    auto* spinBox = new QDoubleSpinBox(parent);
    spinBox->setRange(min, max);
    spinBox->setSingleStep(step);
    spinBox->setDecimals(3);
    spinBox->setFixedWidth(80);
    return spinBox;
}

void MainWindowShell::maybePromptForGltfConversion(const QString& rootPath)
{
    const QString absoluteRoot = QFileInfo(rootPath).absoluteFilePath();
    if (absoluteRoot.isEmpty() || m_promptedConversionRoots.indexOf(absoluteRoot) != -1)
    {
        return;
    }

    static const QStringList convertibleSuffixes = {
        QStringLiteral("obj"),
        QStringLiteral("dae"),
        QStringLiteral("3ds"),
        QStringLiteral("blend")
    };

    QDir dir(absoluteRoot);
    if (!dir.exists())
    {
        return;
    }

    const QList<QFileInfo> missingEntries = findConvertibleSourcesWithoutGltf(absoluteRoot, convertibleSuffixes);
    QStringList missingConversions;
    for (const QFileInfo& entry : missingEntries)
    {
        missingConversions.push_back(dir.relativeFilePath(entry.absoluteFilePath()));
    }

    if (missingConversions.isEmpty())
    {
        m_promptedConversionRoots.push_back(absoluteRoot);
        return;
    }

    QString commandPreview;
    for (const QFileInfo& entry : missingEntries)
    {
        const auto command = conversionCommandForFile(entry);
        if (command.has_value())
        {
            commandPreview = formatCommandLine(*command);
            break;
        }
    }
    if (commandPreview.isEmpty())
    {
        commandPreview = missingConverterMessageForFile(missingEntries.front());
    }

    QMessageBox prompt(this);
    prompt.setIcon(QMessageBox::Question);
    prompt.setWindowTitle(QStringLiteral("Convert To GLTF"));
    prompt.setText(QStringLiteral("Convert source assets in this folder tree to GLTF?"));
    prompt.setInformativeText(
        QStringLiteral("The following files do not have a matching .gltf file:\n%1\n\nConverter command:\n%2")
            .arg(missingConversions.join(QStringLiteral("\n")), commandPreview));
    prompt.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    prompt.setDefaultButton(QMessageBox::Yes);
    const int choice = prompt.exec();

    if (choice == QMessageBox::Yes)
    {
        QStringList converted;
        QStringList failed;

        for (const QFileInfo& entry : missingEntries)
        {
            QString errorMessage;
            if (convertSourceFileToGltf(entry, &errorMessage))
            {
                converted.push_back(dir.relativeFilePath(entry.absoluteFilePath()));
            }
            else
            {
                failed.push_back(QStringLiteral("%1: %2").arg(dir.relativeFilePath(entry.absoluteFilePath()), errorMessage));
            }
        }

        if (m_assetBrowser)
        {
            m_assetBrowser->setRootPath(absoluteRoot);
        }

        QMessageBox result(this);
        result.setWindowTitle(QStringLiteral("GLTF Conversion"));
        result.setIcon(failed.isEmpty() ? QMessageBox::Information : QMessageBox::Warning);
        result.setText(failed.isEmpty()
                           ? QStringLiteral("Conversion completed.")
                           : QStringLiteral("Conversion finished with some failures."));
        QString details;
        if (!converted.isEmpty())
        {
            details += QStringLiteral("Converted:\n%1").arg(converted.join(QStringLiteral("\n")));
        }
        if (!failed.isEmpty())
        {
            if (!details.isEmpty())
            {
                details += QStringLiteral("\n\n");
            }
            details += QStringLiteral("Failed:\n%1").arg(failed.join(QStringLiteral("\n")));
        }
        result.setInformativeText(details.isEmpty() ? QStringLiteral("No files needed conversion.") : details);
        result.exec();
    }

    if (m_promptedConversionRoots.indexOf(absoluteRoot) == -1)
    {
        m_promptedConversionRoots.push_back(absoluteRoot);
    }
}

void MainWindowShell::setupCameraSettingsPanel()
{
    if (!m_rightTabs)
    {
        return;
    }

    auto* cameraPanel = new QWidget(m_rightTabs);
    auto* cameraLayout = new QFormLayout(cameraPanel);

    m_renderPathCombo = new QComboBox(cameraPanel);
    m_renderPathCombo->addItem(QStringLiteral("Forward 3D"), QStringLiteral("forward3d"));
    m_renderPathCombo->addItem(QStringLiteral("Flat 2D"), QStringLiteral("flat2d"));
    cameraLayout->addRow(QStringLiteral("Render Path"), m_renderPathCombo);
    connect(m_renderPathCombo, &QComboBox::currentIndexChanged, this, [this]() {
        if (m_updatingCameraSettings || !m_viewportHost || !m_renderPathCombo) return;
        m_viewportHost->setRenderPath(m_renderPathCombo->currentData().toString());
        saveProjectState();
    });
    
    // Camera speed
    m_cameraSpeedSpin = createSpinBox(cameraPanel, 0.001, 10.0, 0.001);
    m_cameraSpeedSpin->setValue(0.01);
    cameraLayout->addRow(QStringLiteral("Camera Speed"), m_cameraSpeedSpin);
    
    connect(m_cameraSpeedSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this]() {
        if (m_updatingCameraSettings || !m_viewportHost) return;
        m_viewportHost->setCameraSpeed(static_cast<float>(m_cameraSpeedSpin->value()));
    });
    
    // Camera position
    auto* camPosWidget = new QWidget(cameraPanel);
    auto* camPosLayout = new QHBoxLayout(camPosWidget);
    camPosLayout->setContentsMargins(0, 0, 0, 0);
    m_cameraPosX = createSpinBox(cameraPanel, -1000.0, 1000.0, 0.01);
    m_cameraPosY = createSpinBox(cameraPanel, -1000.0, 1000.0, 0.01);
    m_cameraPosZ = createSpinBox(cameraPanel, -1000.0, 1000.0, 0.01);
    camPosLayout->addWidget(new QLabel("X:", camPosWidget));
    camPosLayout->addWidget(m_cameraPosX);
    camPosLayout->addWidget(new QLabel("Y:", camPosWidget));
    camPosLayout->addWidget(m_cameraPosY);
    camPosLayout->addWidget(new QLabel("Z:", camPosWidget));
    camPosLayout->addWidget(m_cameraPosZ);
    cameraLayout->addRow(QStringLiteral("Camera Position"), camPosWidget);
    
    auto applyCameraPos = [this]() {
        if (m_updatingCameraSettings || !m_viewportHost) return;
        QVector3D pos(m_cameraPosX->value(), m_cameraPosY->value(), m_cameraPosZ->value());
        m_viewportHost->setCameraPosition(pos);
        saveProjectState();
    };
    connect(m_cameraPosX, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, applyCameraPos);
    connect(m_cameraPosY, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, applyCameraPos);
    connect(m_cameraPosZ, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, applyCameraPos);
    
    // Camera rotation
    auto* camRotWidget = new QWidget(cameraPanel);
    auto* camRotLayout = new QHBoxLayout(camRotWidget);
    camRotLayout->setContentsMargins(0, 0, 0, 0);
    m_cameraRotX = createSpinBox(cameraPanel, -360.0, 360.0, 0.1);
    m_cameraRotY = createSpinBox(cameraPanel, -360.0, 360.0, 0.1);
    m_cameraRotZ = createSpinBox(cameraPanel, -360.0, 360.0, 0.1);
    camRotLayout->addWidget(new QLabel("X:", camRotWidget));
    camRotLayout->addWidget(m_cameraRotX);
    camRotLayout->addWidget(new QLabel("Y:", camRotWidget));
    camRotLayout->addWidget(m_cameraRotY);
    camRotLayout->addWidget(new QLabel("Z:", camRotWidget));
    camRotLayout->addWidget(m_cameraRotZ);
    cameraLayout->addRow(QStringLiteral("Camera Rotation"), camRotWidget);
    
    auto applyCameraRot = [this]() {
        if (m_updatingCameraSettings || !m_viewportHost) return;
        QVector3D rot(m_cameraRotX->value(), m_cameraRotY->value(), m_cameraRotZ->value());
        m_viewportHost->setCameraRotation(rot);
        saveProjectState();
    };
    connect(m_cameraRotX, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, applyCameraRot);
    connect(m_cameraRotY, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, applyCameraRot);
    connect(m_cameraRotZ, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, applyCameraRot);
    
    // Background color
    m_bgColorWidget = new QWidget(cameraPanel);
    m_bgColorWidget->setFixedSize(60, 30);
    m_bgColorWidget->setStyleSheet(QStringLiteral("background-color: #3333cc; border: 1px solid #888;"));
    auto* bgButton = new QPushButton(QStringLiteral("Change Background"), cameraPanel);
    auto* bgLayout = new QHBoxLayout();
    bgLayout->addWidget(m_bgColorWidget);
    bgLayout->addWidget(bgButton);
    bgLayout->addStretch(1);
    cameraLayout->addRow(QStringLiteral("Background"), bgLayout);
    
    connect(bgButton, &QPushButton::clicked, this, [this]() {
        if (!m_viewportHost) return;
        QColor color = QColorDialog::getColor(Qt::darkBlue, this, QStringLiteral("Select Background Color"));
        if (color.isValid())
        {
            m_bgColorWidget->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid #888;").arg(color.name()));
            m_viewportHost->setBackgroundColor(color);
            saveProjectState();
        }
    });
    
    // Reset camera button
    auto* resetCamButton = new QPushButton(QStringLiteral("Reset Camera"), cameraPanel);
    cameraLayout->addRow(resetCamButton);
    connect(resetCamButton, &QPushButton::clicked, this, [this]() {
        if (!m_viewportHost) return;
        m_viewportHost->resetCamera();
        updateCameraSettingsPanel();
        saveProjectState();
    });

    m_rightTabs->addTab(cameraPanel, QStringLiteral("Camera/Scene"));
}

void MainWindowShell::updateCameraSettingsPanel()
{
    if (!m_viewportHost || m_updatingCameraSettings) return;
    m_updatingCameraSettings = true;
    
    QVector3D pos = m_viewportHost->cameraPosition();
    m_cameraPosX->setValue(pos.x());
    m_cameraPosY->setValue(pos.y());
    m_cameraPosZ->setValue(pos.z());
    
    QVector3D rot = m_viewportHost->cameraRotation();
    m_cameraRotX->setValue(rot.x());
    m_cameraRotY->setValue(rot.y());
    m_cameraRotZ->setValue(rot.z());
    if (m_renderPathCombo)
    {
        const int index = m_renderPathCombo->findData(m_viewportHost->renderPath());
        if (index >= 0)
        {
            m_renderPathCombo->setCurrentIndex(index);
        }
    }
    
    m_updatingCameraSettings = false;
}

void MainWindowShell::applyCameraSettings()
{
    saveProjectState();
}

}  // namespace motive::ui
