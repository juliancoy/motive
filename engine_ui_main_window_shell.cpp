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
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QPixmap>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QSplitter>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>
#include <QColorDialog>
#include <QTabWidget>
#include <QDebug>

namespace motive::ui {

namespace {

constexpr int kHierarchyCameraIndex = -1000;
constexpr int kHierarchyLightIndex = -1001;

QJsonObject sceneLightToJson(const ViewportHostWidget::SceneLight& light)
{
    return QJsonObject{
        {QStringLiteral("type"), light.type},
        {QStringLiteral("exists"), light.exists},
        {QStringLiteral("color"), QJsonArray{light.color.x(), light.color.y(), light.color.z()}},
        {QStringLiteral("brightness"), light.brightness},
        {QStringLiteral("direction"), QJsonArray{light.direction.x(), light.direction.y(), light.direction.z()}},
        {QStringLiteral("ambient"), QJsonArray{light.ambient.x(), light.ambient.y(), light.ambient.z()}},
        {QStringLiteral("diffuse"), QJsonArray{light.diffuse.x(), light.diffuse.y(), light.diffuse.z()}}
    };
}

ViewportHostWidget::SceneLight sceneLightFromJson(const QJsonObject& object)
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

    ViewportHostWidget::SceneLight light;
    light.type = object.value(QStringLiteral("type")).toString(QStringLiteral("directional"));
    light.exists = object.value(QStringLiteral("exists")).toBool(false);
    light.color = readVector(object.value(QStringLiteral("color")), QVector3D(1.0f, 1.0f, 1.0f));
    light.brightness = static_cast<float>(object.value(QStringLiteral("brightness")).toDouble(1.0));
    light.direction = readVector(object.value(QStringLiteral("direction")), QVector3D(0.0f, 0.0f, 1.0f));
    light.ambient = readVector(object.value(QStringLiteral("ambient")), QVector3D(0.1f, 0.1f, 0.1f));
    light.diffuse = readVector(object.value(QStringLiteral("diffuse")), QVector3D(0.9f, 0.9f, 0.9f));
    return light;
}

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
    m_hierarchyTree = new QTreeWidget(m_leftPane);
    m_hierarchyTree->setColumnCount(1);
    m_hierarchyTree->setHeaderHidden(true);
    m_hierarchyTree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_hierarchyTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_hierarchyTree, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& pos)
    {
        QTreeWidgetItem* item = m_hierarchyTree->itemAt(pos);
        const int row = item ? item->data(0, Qt::UserRole).toInt() : -1;
        if (!item)
        {
            QMenu menu(this);
            QAction* createLightAction = menu.addAction(QStringLiteral("Create Light"));
            if (m_viewportHost && m_viewportHost->hasSceneLight())
            {
                createLightAction->setEnabled(false);
            }
            if (menu.exec(m_hierarchyTree->mapToGlobal(pos)) == createLightAction && m_viewportHost)
            {
                m_viewportHost->createSceneLight();
                refreshHierarchy(m_viewportHost->sceneItems());
                QList<QTreeWidgetItem*> matches = m_hierarchyTree->findItems(QStringLiteral("Directional Light"), Qt::MatchExactly | Qt::MatchRecursive);
                if (!matches.isEmpty())
                {
                    m_hierarchyTree->setCurrentItem(matches.front());
                }
            }
            return;
        }
        if (row < 0 || row >= m_sceneItems.size() || !m_viewportHost)
        {
            return;
        }
        QMenu menu(this);
        QAction* renameAction = menu.addAction(QStringLiteral("Rename"));
        QAction* focusAction = menu.addAction(QStringLiteral("Focus"));
        QAction* relocateAction = menu.addAction(QStringLiteral("Relocate in front of camera"));
        QAction* visibilityAction = menu.addAction(m_sceneItems[row].visible ? QStringLiteral("Hide") : QStringLiteral("Show"));
        QAction* deleteAction = menu.addAction(QStringLiteral("Delete"));
        QAction* chosen = menu.exec(m_hierarchyTree->mapToGlobal(pos));
        if (chosen == renameAction)
        {
            const QString currentName = m_sceneItems[row].name;
            bool ok = false;
            const QString name = QInputDialog::getText(this,
                                                       QStringLiteral("Rename Element"),
                                                       QStringLiteral("Name:"),
                                                       QLineEdit::Normal,
                                                       currentName,
                                                       &ok);
            if (ok && !name.trimmed().isEmpty())
            {
                m_viewportHost->renameSceneItem(row, name);
                updateInspectorForSelection(m_hierarchyTree->currentItem());
            }
        }
        else if (chosen == focusAction)
        {
            m_viewportHost->focusSceneItem(row);
            updateInspectorForSelection(m_hierarchyTree->currentItem());
        }
        else if (chosen == relocateAction)
        {
            m_viewportHost->relocateSceneItemInFrontOfCamera(row);
            updateInspectorForSelection(m_hierarchyTree->currentItem());
        }
        else if (chosen == visibilityAction)
        {
            m_viewportHost->setSceneItemVisible(row, !m_sceneItems[row].visible);
            updateInspectorForSelection(m_hierarchyTree->currentItem());
        }
        else if (chosen == deleteAction)
        {
            m_viewportHost->deleteSceneItem(row);
            updateInspectorForSelection(m_hierarchyTree->currentItem());
        }
    });
    leftLayout->addWidget(m_hierarchyTree, 0);

    // Apply validation layers setting before viewport initializes
    if (!m_projectSession.currentValidationLayersEnabled())
    {
        qputenv("MOTIVE_DISABLE_VALIDATION", "1");
    }
    else
    {
        qunsetenv("MOTIVE_DISABLE_VALIDATION");
    }
    
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
        if (m_hierarchyTree && m_hierarchyTree->currentItem() &&
            m_hierarchyTree->currentItem()->data(0, Qt::UserRole).toInt() == kHierarchyCameraIndex)
        {
            updateInspectorForSelection(m_hierarchyTree->currentItem());
        }
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
    m_animationModeValue = new QLabel(QStringLiteral("-"), inspectorPanel);
    m_inspectorTexturePreview = new QLabel(QStringLiteral("No texture"), inspectorPanel);
    m_inspectorTexturePreview->setMinimumSize(160, 160);
    m_inspectorTexturePreview->setAlignment(Qt::AlignCenter);
    m_inspectorTexturePreview->setFrameShape(QFrame::StyledPanel);
    m_primitiveCullModeCombo = new QComboBox(inspectorPanel);
    m_primitiveCullModeCombo->addItem(QStringLiteral("Back"), QStringLiteral("back"));
    m_primitiveCullModeCombo->addItem(QStringLiteral("None"), QStringLiteral("none"));
    m_primitiveCullModeCombo->addItem(QStringLiteral("Front"), QStringLiteral("front"));
    m_primitiveForceAlphaButton = new QPushButton(QStringLiteral("Set Alpha 1"), inspectorPanel);
    m_primitiveForceAlphaButton->setCheckable(true);
    m_loadMeshConsolidationCheck = new QCheckBox(QStringLiteral("Enable mesh consolidation"), inspectorPanel);
    m_paintOverrideCheck = new QCheckBox(QStringLiteral("Paint all verts"), inspectorPanel);
    m_paintColorWidget = new QWidget(inspectorPanel);
    m_paintColorWidget->setFixedSize(60, 24);
    m_paintColorWidget->setStyleSheet(QStringLiteral("background-color: #ff00ff; border: 1px solid #888;"));
    m_paintColorWidget->setProperty("paintColor", QStringLiteral("#ff00ff"));
    auto* paintColorButton = new QPushButton(QStringLiteral("Choose"), inspectorPanel);
    auto* paintColorContainer = new QWidget(inspectorPanel);
    auto* paintColorLayout = new QHBoxLayout(paintColorContainer);
    paintColorLayout->setContentsMargins(0, 0, 0, 0);
    paintColorLayout->addWidget(m_paintColorWidget);
    paintColorLayout->addWidget(paintColorButton);
    paintColorLayout->addStretch(1);
    m_animationControlsWidget = new QWidget(inspectorPanel);
    auto* animationControlsLayout = new QVBoxLayout(m_animationControlsWidget);
    animationControlsLayout->setContentsMargins(0, 0, 0, 0);
    animationControlsLayout->setSpacing(6);
    m_animationClipCombo = new QComboBox(m_animationControlsWidget);
    m_animationPlayingCheck = new QCheckBox(QStringLiteral("Playing"), m_animationControlsWidget);
    m_animationLoopCheck = new QCheckBox(QStringLiteral("Loop"), m_animationControlsWidget);
    m_animationSpeedSpin = createSpinBox(m_animationControlsWidget, 0.0, 10.0, 0.01);
    auto* animationFlagsWidget = new QWidget(m_animationControlsWidget);
    auto* animationFlagsLayout = new QHBoxLayout(animationFlagsWidget);
    animationFlagsLayout->setContentsMargins(0, 0, 0, 0);
    animationFlagsLayout->addWidget(m_animationPlayingCheck);
    animationFlagsLayout->addWidget(m_animationLoopCheck);
    animationFlagsLayout->addStretch(1);
    auto* animationSpeedWidget = new QWidget(m_animationControlsWidget);
    auto* animationSpeedLayout = new QHBoxLayout(animationSpeedWidget);
    animationSpeedLayout->setContentsMargins(0, 0, 0, 0);
    animationSpeedLayout->addWidget(new QLabel(QStringLiteral("Speed:"), animationSpeedWidget));
    animationSpeedLayout->addWidget(m_animationSpeedSpin);
    animationSpeedLayout->addStretch(1);
    animationControlsLayout->addWidget(m_animationClipCombo);
    animationControlsLayout->addWidget(animationFlagsWidget);
    animationControlsLayout->addWidget(animationSpeedWidget);

    m_lightTypeCombo = new QComboBox(inspectorPanel);
    m_lightTypeCombo->addItem(QStringLiteral("Directional"), QStringLiteral("directional"));
    m_lightTypeCombo->addItem(QStringLiteral("Ambient"), QStringLiteral("ambient"));
    m_lightTypeCombo->addItem(QStringLiteral("Hemispherical"), QStringLiteral("hemispherical"));
    m_lightBrightnessSpin = createSpinBox(inspectorPanel, 0.0, 100.0, 0.01);
    m_lightColorWidget = new QWidget(inspectorPanel);
    m_lightColorWidget->setFixedSize(60, 24);
    m_lightColorWidget->setStyleSheet(QStringLiteral("background-color: #ffffff; border: 1px solid #888;"));
    auto* lightColorButton = new QPushButton(QStringLiteral("Change"), inspectorPanel);
    auto* lightColorContainer = new QWidget(inspectorPanel);
    auto* lightColorLayout = new QHBoxLayout(lightColorContainer);
    lightColorLayout->setContentsMargins(0, 0, 0, 0);
    lightColorLayout->addWidget(m_lightColorWidget);
    lightColorLayout->addWidget(lightColorButton);
    lightColorLayout->addStretch(1);

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
    inspectorLayout->addRow(QStringLiteral("Animation Path"), m_animationModeValue);
    inspectorLayout->addRow(QStringLiteral("Texture"), m_inspectorTexturePreview);
    inspectorLayout->addRow(QStringLiteral("Load"), m_loadMeshConsolidationCheck);
    inspectorLayout->addRow(QStringLiteral("Cull Mode"), m_primitiveCullModeCombo);
    inspectorLayout->addRow(QStringLiteral("Opacity"), m_primitiveForceAlphaButton);
    inspectorLayout->addRow(QStringLiteral("Paint Override"), m_paintOverrideCheck);
    inspectorLayout->addRow(QStringLiteral("Paint Color"), paintColorContainer);
    inspectorLayout->addRow(QStringLiteral("Animation"), m_animationControlsWidget);
    inspectorLayout->addRow(QStringLiteral("Light Type"), m_lightTypeCombo);
    inspectorLayout->addRow(QStringLiteral("Brightness"), m_lightBrightnessSpin);
    inspectorLayout->addRow(QStringLiteral("Color"), lightColorContainer);
    inspectorLayout->addRow(QStringLiteral("Translation"), translationWidget);
    inspectorLayout->addRow(QStringLiteral("Rotation"), rotationWidget);
    inspectorLayout->addRow(QStringLiteral("Scale"), scaleWidget);
    m_rightTabs->addTab(inspectorPanel, QStringLiteral("Element"));
    inspectorDock->setWidget(m_rightTabs);
    addDockWidget(Qt::RightDockWidgetArea, inspectorDock);

    setupCameraSettingsPanel();

    connect(m_hierarchyTree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* current)
    {
        updateInspectorForSelection(current);
    });

    auto applyAnimationInspector = [this]() {
        if (m_updatingInspector || !m_viewportHost || !m_hierarchyTree) return;
        QTreeWidgetItem* current = m_hierarchyTree->currentItem();
        const int row = current ? current->data(0, Qt::UserRole).toInt() : -1;
        if (row < 0 || row >= m_sceneItems.size() || !m_animationClipCombo || !m_animationPlayingCheck || !m_animationLoopCheck || !m_animationSpeedSpin)
        {
            return;
        }
        m_viewportHost->updateSceneItemAnimationState(row,
                                                      m_animationClipCombo->currentData().toString(),
                                                      m_animationPlayingCheck->isChecked(),
                                                      m_animationLoopCheck->isChecked(),
                                                      static_cast<float>(m_animationSpeedSpin->value()));
        m_sceneItems[row].activeAnimationClip = m_animationClipCombo->currentData().toString();
        m_sceneItems[row].animationPlaying = m_animationPlayingCheck->isChecked();
        m_sceneItems[row].animationLoop = m_animationLoopCheck->isChecked();
        m_sceneItems[row].animationSpeed = static_cast<float>(m_animationSpeedSpin->value());
        saveProjectState();
    };
    connect(m_animationClipCombo, &QComboBox::currentIndexChanged, this, [applyAnimationInspector]() { applyAnimationInspector(); });
    connect(m_animationPlayingCheck, &QCheckBox::toggled, this, [applyAnimationInspector](bool) { applyAnimationInspector(); });
    connect(m_animationLoopCheck, &QCheckBox::toggled, this, [applyAnimationInspector](bool) { applyAnimationInspector(); });
    connect(m_animationSpeedSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [applyAnimationInspector](double) { applyAnimationInspector(); });

    connect(m_primitiveCullModeCombo, &QComboBox::currentIndexChanged, this, [this]() {
        if (m_updatingInspector || !m_viewportHost || !m_hierarchyTree || !m_primitiveCullModeCombo)
        {
            return;
        }
        QTreeWidgetItem* current = m_hierarchyTree->currentItem();
        const int row = current ? current->data(0, Qt::UserRole).toInt() : -1;
        const int meshIndex = current ? current->data(0, Qt::UserRole + 1).toInt() : -1;
        const int primitiveIndex = current ? current->data(0, Qt::UserRole + 2).toInt() : -1;
        if (row < 0 || meshIndex < 0 || primitiveIndex < 0)
        {
            return;
        }
        m_viewportHost->setPrimitiveCullMode(row, meshIndex, primitiveIndex, m_primitiveCullModeCombo->currentData().toString());
    });
    connect(m_primitiveForceAlphaButton, &QPushButton::toggled, this, [this](bool enabled) {
        if (m_updatingInspector || !m_viewportHost || !m_hierarchyTree)
        {
            return;
        }
        QTreeWidgetItem* current = m_hierarchyTree->currentItem();
        const int row = current ? current->data(0, Qt::UserRole).toInt() : -1;
        const int meshIndex = current ? current->data(0, Qt::UserRole + 1).toInt() : -1;
        const int primitiveIndex = current ? current->data(0, Qt::UserRole + 2).toInt() : -1;
        if (row < 0 || meshIndex < 0 || primitiveIndex < 0)
        {
            return;
        }
        m_viewportHost->setPrimitiveForceAlphaOne(row, meshIndex, primitiveIndex, enabled);
        m_primitiveForceAlphaButton->setText(enabled ? QStringLiteral("Alpha forced to 1")
                                                     : QStringLiteral("Set Alpha 1"));
    });
    connect(m_loadMeshConsolidationCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (m_updatingInspector || !m_viewportHost || !m_hierarchyTree)
        {
            return;
        }
        QTreeWidgetItem* current = m_hierarchyTree->currentItem();
        const int row = current ? current->data(0, Qt::UserRole).toInt() : -1;
        if (row < 0 || row >= m_sceneItems.size())
        {
            return;
        }
        m_viewportHost->setSceneItemMeshConsolidationEnabled(row, checked);
        m_sceneItems[row].meshConsolidationEnabled = checked;
        saveProjectState();
    });

    auto applyPaintInspector = [this]() {
        if (m_updatingInspector || !m_viewportHost || !m_hierarchyTree || !m_paintOverrideCheck)
        {
            return;
        }
        QTreeWidgetItem* current = m_hierarchyTree->currentItem();
        const int row = current ? current->data(0, Qt::UserRole).toInt() : -1;
        if (row < 0 || row >= m_sceneItems.size())
        {
            return;
        }
        QColor color = QColor(m_paintColorWidget && m_paintColorWidget->property("paintColor").isValid()
                                  ? m_paintColorWidget->property("paintColor").toString()
                                  : QStringLiteral("#ff00ff"));
        const QVector3D paintColor(static_cast<float>(color.redF()),
                                   static_cast<float>(color.greenF()),
                                   static_cast<float>(color.blueF()));
        m_viewportHost->updateSceneItemPaintOverride(row, m_paintOverrideCheck->isChecked(), paintColor);
        m_sceneItems[row].paintOverrideEnabled = m_paintOverrideCheck->isChecked();
        m_sceneItems[row].paintOverrideColor = paintColor;
        saveProjectState();
    };
    connect(m_paintOverrideCheck, &QCheckBox::toggled, this, [applyPaintInspector](bool) { applyPaintInspector(); });
    connect(paintColorButton, &QPushButton::clicked, this, [this, applyPaintInspector]() {
        if (m_updatingInspector || !m_paintColorWidget)
        {
            return;
        }
        QColor initial = QColor(m_paintColorWidget->property("paintColor").toString());
        if (!initial.isValid()) initial = QColor(QStringLiteral("#ff00ff"));
        const QColor color = QColorDialog::getColor(initial, this, QStringLiteral("Select Paint Override Color"));
        if (!color.isValid())
        {
            return;
        }
        m_paintColorWidget->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid #888;").arg(color.name()));
        m_paintColorWidget->setProperty("paintColor", color.name());
        applyPaintInspector();
    });

    auto applyLightInspector = [this]() {
        if (m_updatingInspector || !m_viewportHost || !m_lightTypeCombo || !m_lightBrightnessSpin) return;
        auto light = m_viewportHost->sceneLight();
        light.exists = true;
        light.type = m_lightTypeCombo->currentData().toString();
        light.brightness = static_cast<float>(m_lightBrightnessSpin->value());
        m_viewportHost->setSceneLight(light);
        saveProjectState();
    };
    connect(m_lightTypeCombo, &QComboBox::currentIndexChanged, this, [applyLightInspector]() { applyLightInspector(); });
    connect(m_lightBrightnessSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [applyLightInspector](double) { applyLightInspector(); });
    connect(lightColorButton, &QPushButton::clicked, this, [this]() {
        if (m_updatingInspector || !m_viewportHost) return;
        auto light = m_viewportHost->sceneLight();
        const QColor initial = QColor::fromRgbF(light.color.x(), light.color.y(), light.color.z());
        const QColor color = QColorDialog::getColor(initial, this, QStringLiteral("Select Light Color"));
        if (!color.isValid())
        {
            return;
        }
        light.exists = true;
        light.color = QVector3D(static_cast<float>(color.redF()),
                                static_cast<float>(color.greenF()),
                                static_cast<float>(color.blueF()));
        if (m_lightColorWidget)
        {
            m_lightColorWidget->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid #888;").arg(color.name()));
        }
        m_viewportHost->setSceneLight(light);
        saveProjectState();
    });

    // Connect spin box value changes to update scene items
    auto updateTransform = [this]() {
        if (m_updatingInspector) return;
        QTreeWidgetItem* current = m_hierarchyTree ? m_hierarchyTree->currentItem() : nullptr;
        const int row = current ? current->data(0, Qt::UserRole).toInt() : -1;
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
        if (row == kHierarchyCameraIndex && m_viewportHost) {
            m_viewportHost->setCameraPosition(translation);
            m_viewportHost->setCameraRotation(rotation);
            updateCameraSettingsPanel();
            saveProjectState();
            return;
        }
        if (row >= 0 && row < m_sceneItems.size()) {
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

QJsonArray MainWindowShell::hierarchyJson() const
{
    return m_viewportHost ? m_viewportHost->hierarchyJson() : QJsonArray{};
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
        m_viewportHost->setSceneLight(sceneLightFromJson(m_projectSession.currentSceneLight()));
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
        m_viewportHost->setSceneLight(sceneLightFromJson(m_projectSession.currentSceneLight()));
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
        m_viewportHost->setSceneLight(sceneLightFromJson(m_projectSession.currentSceneLight()));
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
    m_projectSession.setCurrentSceneLight(m_viewportHost ? sceneLightToJson(m_viewportHost->sceneLight()) : QJsonObject{});
    m_projectSession.setCurrentRenderPath(m_viewportHost ? m_viewportHost->renderPath() : QStringLiteral("forward3d"));
    m_projectSession.setCurrentMeshConsolidationEnabled(m_viewportHost ? m_viewportHost->meshConsolidationEnabled() : true);
    qDebug() << "[MainWindowShell] Saving project state"
             << "projectId=" << m_projectSession.currentProjectId()
             << "root=" << (m_assetBrowser ? m_assetBrowser->rootPath() : QDir::currentPath())
             << "sceneCount=" << (m_viewportHost ? m_viewportHost->sceneItems().size() : 0);
    m_projectSession.saveCurrentProject();
}

void MainWindowShell::refreshHierarchy(const QList<ViewportHostWidget::SceneItem>& items)
{
    if (!m_hierarchyTree)
    {
        return;
    }

    m_sceneItems = items;
    QTreeWidgetItem* previousItem = m_hierarchyTree->currentItem();
    const int previousRow = previousItem ? previousItem->data(0, Qt::UserRole).toInt() : -1;
    const int previousMeshIndex = previousItem ? previousItem->data(0, Qt::UserRole + 1).toInt() : -1;
    const int previousPrimitiveIndex = previousItem ? previousItem->data(0, Qt::UserRole + 2).toInt() : -1;
    const int previousType = previousItem ? previousItem->data(0, Qt::UserRole + 3).toInt() : -1;
    const QString previousClipName = previousItem ? previousItem->data(0, Qt::UserRole + 4).toString() : QString();
    m_hierarchyTree->clear();

    const QList<ViewportHostWidget::HierarchyNode> hierarchyItems =
        m_viewportHost ? m_viewportHost->hierarchyItems() : QList<ViewportHostWidget::HierarchyNode>{};
    for (const auto& node : hierarchyItems)
    {
        appendHierarchyNode(nullptr, node, false);
    }

    if (previousItem)
    {
        QList<QTreeWidgetItem*> matches = m_hierarchyTree->findItems(QStringLiteral("*"), Qt::MatchWildcard | Qt::MatchRecursive);
        QTreeWidgetItem* fallbackRowMatch = nullptr;
        for (QTreeWidgetItem* item : matches)
        {
            if (!item || item->data(0, Qt::UserRole).toInt() != previousRow)
            {
                continue;
            }
            if (!fallbackRowMatch)
            {
                fallbackRowMatch = item;
            }
            if (item->data(0, Qt::UserRole + 1).toInt() == previousMeshIndex &&
                item->data(0, Qt::UserRole + 2).toInt() == previousPrimitiveIndex &&
                item->data(0, Qt::UserRole + 3).toInt() == previousType &&
                item->data(0, Qt::UserRole + 4).toString() == previousClipName)
            {
                m_hierarchyTree->setCurrentItem(item);
                return;
            }
        }
        if (fallbackRowMatch)
        {
            m_hierarchyTree->setCurrentItem(fallbackRowMatch);
            return;
        }
    }

    if (!items.isEmpty())
    {
        m_hierarchyTree->setCurrentItem(m_hierarchyTree->topLevelItem(0));
    }
    else
    {
        updateInspectorForSelection(nullptr);
    }
}

void MainWindowShell::appendHierarchyNode(QTreeWidgetItem* parent, const ViewportHostWidget::HierarchyNode& node, bool ancestorHidden)
{
    if (!m_hierarchyTree)
    {
        return;
    }

    QTreeWidgetItem* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_hierarchyTree);
    item->setText(0, node.label);
    item->setData(0, Qt::UserRole, node.sceneIndex);
    item->setData(0, Qt::UserRole + 1, node.meshIndex);
    item->setData(0, Qt::UserRole + 2, node.primitiveIndex);
    item->setData(0, Qt::UserRole + 3, static_cast<int>(node.type));
    item->setData(0, Qt::UserRole + 4, node.clipName);
    const bool selfHidden = node.sceneIndex >= 0 && node.sceneIndex < m_sceneItems.size() && !m_sceneItems[node.sceneIndex].visible;
    const bool hidden = ancestorHidden || selfHidden;
    QFont font = item->font(0);
    font.setItalic(hidden);
    item->setFont(0, font);

    if (node.children.isEmpty() && node.sceneIndex >= 0 && node.sceneIndex < m_sceneItems.size())
    {
        const auto& sceneItem = m_sceneItems[node.sceneIndex];
        item->setToolTip(0, QDir::toNativeSeparators(sceneItem.sourcePath));
    }

    for (const auto& child : node.children)
    {
        appendHierarchyNode(item, child, hidden);
    }

    if (!node.children.isEmpty())
    {
        item->setExpanded(true);
    }
}

void MainWindowShell::updateInspectorForSelection(QTreeWidgetItem* currentItem)
{
    m_updatingInspector = true;
    const int row = currentItem ? currentItem->data(0, Qt::UserRole).toInt() : -1;
    const int meshIndex = currentItem ? currentItem->data(0, Qt::UserRole + 1).toInt() : -1;
    const int primitiveIndex = currentItem ? currentItem->data(0, Qt::UserRole + 2).toInt() : -1;
    const int nodeType = currentItem ? currentItem->data(0, Qt::UserRole + 3).toInt() : -1;
    const QString clipName = currentItem ? currentItem->data(0, Qt::UserRole + 4).toString() : QString();
    const bool valid = row >= 0 && row < m_sceneItems.size();
    const auto setValue = [](QLabel* label, const QString& value)
    {
        if (label)
        {
            label->setText(value);
        }
    };
    const auto setLightInspectorVisible = [this](bool visible)
    {
        if (m_lightTypeCombo) m_lightTypeCombo->setVisible(visible);
        if (m_lightBrightnessSpin) m_lightBrightnessSpin->setVisible(visible);
        if (m_lightColorWidget && m_lightColorWidget->parentWidget()) m_lightColorWidget->parentWidget()->setVisible(visible);
    };
    const auto setPrimitiveInspectorVisible = [this](bool visible, const QString& cullMode = QStringLiteral("back"))
    {
        if (m_primitiveCullModeCombo)
        {
            m_primitiveCullModeCombo->setVisible(visible);
            if (visible)
            {
                const int index = m_primitiveCullModeCombo->findData(cullMode);
                if (index >= 0)
                {
                    m_primitiveCullModeCombo->setCurrentIndex(index);
                }
            }
        }
        if (m_primitiveForceAlphaButton)
        {
            m_primitiveForceAlphaButton->setVisible(visible);
        }
        if (m_paintOverrideCheck)
        {
            m_paintOverrideCheck->setVisible(visible);
        }
        if (m_paintColorWidget && m_paintColorWidget->parentWidget())
        {
            m_paintColorWidget->parentWidget()->setVisible(visible);
        }
    };
    const auto setLoadInspectorVisible = [this](bool visible, bool meshConsolidationEnabled = true)
    {
        if (m_loadMeshConsolidationCheck)
        {
            m_loadMeshConsolidationCheck->setVisible(visible);
            if (visible)
            {
                m_loadMeshConsolidationCheck->setChecked(meshConsolidationEnabled);
            }
        }
    };
    const auto setAnimationInspector = [this](bool visible,
                                              const QStringList& clips = {},
                                              const QString& activeClip = QString(),
                                              bool playing = true,
                                              bool loop = true,
                                              float speed = 1.0f)
    {
        if (m_animationControlsWidget)
        {
            m_animationControlsWidget->setVisible(visible);
        }
        if (!visible || !m_animationClipCombo || !m_animationPlayingCheck || !m_animationLoopCheck || !m_animationSpeedSpin)
        {
            return;
        }
        m_animationClipCombo->blockSignals(true);
        m_animationClipCombo->clear();
        for (const QString& clip : clips)
        {
            m_animationClipCombo->addItem(clip, clip);
        }
        int clipIndex = activeClip.isEmpty() ? -1 : m_animationClipCombo->findData(activeClip);
        if (clipIndex < 0 && m_animationClipCombo->count() > 0)
        {
            clipIndex = 0;
        }
        if (clipIndex >= 0)
        {
            m_animationClipCombo->setCurrentIndex(clipIndex);
        }
        m_animationClipCombo->blockSignals(false);
        m_animationPlayingCheck->setChecked(playing);
        m_animationLoopCheck->setChecked(loop);
        m_animationSpeedSpin->setValue(speed);
    };
    const auto setTexturePreview = [this](const QImage& image)
    {
        if (!m_inspectorTexturePreview)
        {
            return;
        }
        if (image.isNull())
        {
            m_inspectorTexturePreview->setPixmap(QPixmap());
            m_inspectorTexturePreview->setText(QStringLiteral("No texture"));
            return;
        }
        const QPixmap pixmap = QPixmap::fromImage(image);
        m_inspectorTexturePreview->setPixmap(pixmap.scaled(m_inspectorTexturePreview->size(),
                                                           Qt::KeepAspectRatio,
                                                           Qt::SmoothTransformation));
        m_inspectorTexturePreview->setText(QString());
    };

    if (!valid)
    {
        if (row == kHierarchyCameraIndex && m_viewportHost)
        {
            setValue(m_inspectorNameValue, QStringLiteral("Camera"));
            setValue(m_inspectorPathValue, QStringLiteral("Viewport Camera"));
            setValue(m_animationModeValue, QStringLiteral("Static"));
            QVector3D pos = m_viewportHost->cameraPosition();
            QVector3D rot = m_viewportHost->cameraRotation();
            if (m_inspectorTranslationX) m_inspectorTranslationX->setValue(pos.x());
            if (m_inspectorTranslationY) m_inspectorTranslationY->setValue(pos.y());
            if (m_inspectorTranslationZ) m_inspectorTranslationZ->setValue(pos.z());
            if (m_inspectorRotationX) m_inspectorRotationX->setValue(rot.x());
            if (m_inspectorRotationY) m_inspectorRotationY->setValue(rot.y());
            if (m_inspectorRotationZ) m_inspectorRotationZ->setValue(rot.z());
            if (m_inspectorScaleX) m_inspectorScaleX->setValue(1.0);
            if (m_inspectorScaleY) m_inspectorScaleY->setValue(1.0);
            if (m_inspectorScaleZ) m_inspectorScaleZ->setValue(1.0);
            setLoadInspectorVisible(false);
            if (m_primitiveForceAlphaButton) m_primitiveForceAlphaButton->setChecked(false);
            if (m_paintOverrideCheck) m_paintOverrideCheck->setChecked(false);
            setPrimitiveInspectorVisible(false);
            setLightInspectorVisible(false);
            setAnimationInspector(false);
            setTexturePreview(QImage());
            m_updatingInspector = false;
            return;
        }
        if (row == kHierarchyLightIndex)
        {
            setValue(m_inspectorNameValue, QStringLiteral("Directional Light"));
            setValue(m_inspectorPathValue, QStringLiteral("Scene Light"));
            setValue(m_animationModeValue, QStringLiteral("Static"));
            if (m_inspectorTranslationX) m_inspectorTranslationX->setValue(0.0);
            if (m_inspectorTranslationY) m_inspectorTranslationY->setValue(0.0);
            if (m_inspectorTranslationZ) m_inspectorTranslationZ->setValue(0.0);
            if (m_inspectorRotationX) m_inspectorRotationX->setValue(0.0);
            if (m_inspectorRotationY) m_inspectorRotationY->setValue(0.0);
            if (m_inspectorRotationZ) m_inspectorRotationZ->setValue(0.0);
            if (m_inspectorScaleX) m_inspectorScaleX->setValue(1.0);
            if (m_inspectorScaleY) m_inspectorScaleY->setValue(1.0);
            if (m_inspectorScaleZ) m_inspectorScaleZ->setValue(1.0);
            setLoadInspectorVisible(false);
            if (m_primitiveForceAlphaButton) m_primitiveForceAlphaButton->setChecked(false);
            if (m_paintOverrideCheck) m_paintOverrideCheck->setChecked(false);
            setPrimitiveInspectorVisible(false);
            setLightInspectorVisible(true);
            setAnimationInspector(false);
            if (m_viewportHost)
            {
                const auto light = m_viewportHost->sceneLight();
                if (m_lightTypeCombo)
                {
                    const int index = m_lightTypeCombo->findData(light.type);
                    if (index >= 0) m_lightTypeCombo->setCurrentIndex(index);
                }
                if (m_lightBrightnessSpin) m_lightBrightnessSpin->setValue(light.brightness);
                if (m_lightColorWidget)
                {
                    const QColor color = QColor::fromRgbF(light.color.x(), light.color.y(), light.color.z());
                    m_lightColorWidget->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid #888;").arg(color.name()));
                }
            }
            setTexturePreview(QImage());
            m_updatingInspector = false;
            return;
        }
        setValue(m_inspectorNameValue, QStringLiteral("-"));
        setValue(m_inspectorPathValue, QStringLiteral("-"));
        setValue(m_animationModeValue, QStringLiteral("-"));
        if (m_inspectorTranslationX) m_inspectorTranslationX->setValue(0.0);
        if (m_inspectorTranslationY) m_inspectorTranslationY->setValue(0.0);
        if (m_inspectorTranslationZ) m_inspectorTranslationZ->setValue(0.0);
        if (m_inspectorRotationX) m_inspectorRotationX->setValue(0.0);
        if (m_inspectorRotationY) m_inspectorRotationY->setValue(0.0);
        if (m_inspectorRotationZ) m_inspectorRotationZ->setValue(0.0);
        if (m_inspectorScaleX) m_inspectorScaleX->setValue(1.0);
        if (m_inspectorScaleY) m_inspectorScaleY->setValue(1.0);
        if (m_inspectorScaleZ) m_inspectorScaleZ->setValue(1.0);
        setLoadInspectorVisible(false);
        if (m_primitiveForceAlphaButton) m_primitiveForceAlphaButton->setChecked(false);
        if (m_paintOverrideCheck) m_paintOverrideCheck->setChecked(false);
        setPrimitiveInspectorVisible(false);
        setLightInspectorVisible(false);
        setAnimationInspector(false);
        setTexturePreview(QImage());
        m_updatingInspector = false;
        return;
    }

    const auto& item = m_sceneItems[row];
    setValue(m_inspectorNameValue, item.name);
    setValue(m_inspectorPathValue, QDir::toNativeSeparators(item.sourcePath));
    setValue(m_animationModeValue,
             m_viewportHost ? m_viewportHost->animationExecutionMode(row, meshIndex, primitiveIndex)
                            : QStringLiteral("-"));
    if (m_inspectorTranslationX) m_inspectorTranslationX->setValue(item.translation.x());
    if (m_inspectorTranslationY) m_inspectorTranslationY->setValue(item.translation.y());
    if (m_inspectorTranslationZ) m_inspectorTranslationZ->setValue(item.translation.z());
    if (m_inspectorRotationX) m_inspectorRotationX->setValue(item.rotation.x());
    if (m_inspectorRotationY) m_inspectorRotationY->setValue(item.rotation.y());
    if (m_inspectorRotationZ) m_inspectorRotationZ->setValue(item.rotation.z());
    if (m_inspectorScaleX) m_inspectorScaleX->setValue(item.scale.x());
    if (m_inspectorScaleY) m_inspectorScaleY->setValue(item.scale.y());
    if (m_inspectorScaleZ) m_inspectorScaleZ->setValue(item.scale.z());
    if (m_paintOverrideCheck) m_paintOverrideCheck->setChecked(item.paintOverrideEnabled);
    if (m_paintColorWidget)
    {
        const QColor color = QColor::fromRgbF(item.paintOverrideColor.x(), item.paintOverrideColor.y(), item.paintOverrideColor.z());
        m_paintColorWidget->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid #888;").arg(color.name()));
        m_paintColorWidget->setProperty("paintColor", color.name());
    }
    const QString suffix = QFileInfo(item.sourcePath).suffix().toLower();
    setLoadInspectorVisible(suffix == QStringLiteral("gltf") || suffix == QStringLiteral("glb"),
                            item.meshConsolidationEnabled);
    setPrimitiveInspectorVisible(m_viewportHost && meshIndex >= 0 && primitiveIndex >= 0,
                                 m_viewportHost ? m_viewportHost->primitiveCullMode(row, meshIndex, primitiveIndex) : QStringLiteral("back"));
    if (m_primitiveForceAlphaButton)
    {
        const bool forceAlphaOne = m_viewportHost && meshIndex >= 0 && primitiveIndex >= 0
            ? m_viewportHost->primitiveForceAlphaOne(row, meshIndex, primitiveIndex)
            : false;
        m_primitiveForceAlphaButton->setChecked(forceAlphaOne);
        m_primitiveForceAlphaButton->setText(forceAlphaOne ? QStringLiteral("Alpha forced to 1")
                                                           : QStringLiteral("Set Alpha 1"));
    }
    setLightInspectorVisible(false);
    QStringList animationClips = m_viewportHost ? m_viewportHost->animationClipNames(row) : QStringList{};
    QString selectedClip = clipName;
    if (selectedClip.isEmpty())
    {
        selectedClip = item.activeAnimationClip;
    }
    setAnimationInspector(!animationClips.isEmpty(),
                          animationClips,
                          selectedClip,
                          item.animationPlaying,
                          item.animationLoop,
                          item.animationSpeed);
    if (m_viewportHost && meshIndex >= 0 && primitiveIndex >= 0)
    {
        setTexturePreview(m_viewportHost->primitiveTexturePreview(row, meshIndex, primitiveIndex));
    }
    else
    {
        setTexturePreview(QImage());
    }
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
            {QStringLiteral("meshConsolidationEnabled"), item.meshConsolidationEnabled},
            {QStringLiteral("translation"), QJsonArray{item.translation.x(), item.translation.y(), item.translation.z()}},
            {QStringLiteral("rotation"), QJsonArray{item.rotation.x(), item.rotation.y(), item.rotation.z()}},
            {QStringLiteral("scale"), QJsonArray{item.scale.x(), item.scale.y(), item.scale.z()}},
            {QStringLiteral("paintOverrideEnabled"), item.paintOverrideEnabled},
            {QStringLiteral("paintOverrideColor"), QJsonArray{item.paintOverrideColor.x(), item.paintOverrideColor.y(), item.paintOverrideColor.z()}},
            {QStringLiteral("activeAnimationClip"), item.activeAnimationClip},
            {QStringLiteral("animationPlaying"), item.animationPlaying},
            {QStringLiteral("animationLoop"), item.animationLoop},
            {QStringLiteral("animationSpeed"), item.animationSpeed},
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
            object.value(QStringLiteral("meshConsolidationEnabled")).toBool(true),
            readVector(object.value(QStringLiteral("translation")), QVector3D(0.0f, 0.0f, 0.0f)),
            readVector(object.value(QStringLiteral("rotation")), QVector3D(-90.0f, 0.0f, 0.0f)),
            readVector(object.value(QStringLiteral("scale")), QVector3D(1.0f, 1.0f, 1.0f)),
            object.value(QStringLiteral("paintOverrideEnabled")).toBool(false),
            readVector(object.value(QStringLiteral("paintOverrideColor")), QVector3D(1.0f, 0.0f, 1.0f)),
            object.value(QStringLiteral("activeAnimationClip")).toString(),
            object.value(QStringLiteral("animationPlaying")).toBool(true),
            object.value(QStringLiteral("animationLoop")).toBool(true),
            static_cast<float>(object.value(QStringLiteral("animationSpeed")).toDouble(1.0)),
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

    m_meshConsolidationCheck = new QCheckBox(QStringLiteral("Enable mesh consolidation"), cameraPanel);
    m_meshConsolidationCheck->setChecked(true);
    cameraLayout->addRow(QStringLiteral("Import"), m_meshConsolidationCheck);
    connect(m_meshConsolidationCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (m_updatingCameraSettings || !m_viewportHost) return;
        m_viewportHost->setMeshConsolidationEnabled(checked);
        saveProjectState();
    });
    
    // Validation layers toggle
    m_validationLayersCheck = new QCheckBox(QStringLiteral("Enable Vulkan validation layers"), cameraPanel);
    m_validationLayersCheck->setChecked(true);
    m_validationRestartLabel = new QLabel(QStringLiteral("(restart required)"), cameraPanel);
    m_validationRestartLabel->setStyleSheet(QStringLiteral("color: #888; font-size: 11px;"));
    m_validationRestartLabel->hide();
    auto* validationLayout = new QHBoxLayout();
    validationLayout->addWidget(m_validationLayersCheck);
    validationLayout->addWidget(m_validationRestartLabel);
    validationLayout->addStretch(1);
    cameraLayout->addRow(QStringLiteral("Debug"), validationLayout);
    connect(m_validationLayersCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (m_updatingCameraSettings) return;
        m_projectSession.setCurrentValidationLayersEnabled(checked);
        saveProjectState();
        // Show restart hint
        if (m_validationRestartLabel) {
            m_validationRestartLabel->show();
        }
    });
    
    // Camera speed
    m_cameraSpeedSpin = createSpinBox(cameraPanel, 0.001, 10.0, 0.001);
    m_cameraSpeedSpin->setValue(0.01);
    cameraLayout->addRow(QStringLiteral("Camera Speed"), m_cameraSpeedSpin);
    
    connect(m_cameraSpeedSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this]() {
        if (m_updatingCameraSettings || !m_viewportHost) return;
        m_viewportHost->setCameraSpeed(static_cast<float>(m_cameraSpeedSpin->value()));
    });
    
    // Free fly camera toggle
    m_freeFlyCameraCheck = new QCheckBox(QStringLiteral("Enable free fly camera (WASD moves camera)"), cameraPanel);
    m_freeFlyCameraCheck->setChecked(true);
    m_freeFlyCameraCheck->setToolTip(QStringLiteral("When enabled, WASD moves the camera. When disabled, WASD controls the character (if character control is enabled)."));
    cameraLayout->addRow(QStringLiteral("Camera Mode"), m_freeFlyCameraCheck);
    connect(m_freeFlyCameraCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (m_updatingCameraSettings) return;
        m_projectSession.setCurrentFreeFlyCameraEnabled(checked);
        saveProjectState();
        // Update viewport camera mode
        if (m_viewportHost) {
            m_viewportHost->setFreeFlyCameraEnabled(checked);
        }
    });
    
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

    m_rightTabs->addTab(cameraPanel, QStringLiteral("Global"));
}

void MainWindowShell::updateCameraSettingsPanel()
{
    if (!m_viewportHost || m_updatingCameraSettings) return;
    m_updatingCameraSettings = true;
    if (m_renderPathCombo)
    {
        const int index = m_renderPathCombo->findData(m_viewportHost->renderPath());
        if (index >= 0)
        {
            m_renderPathCombo->setCurrentIndex(index);
        }
    }
    if (m_meshConsolidationCheck)
    {
        m_meshConsolidationCheck->setChecked(m_viewportHost->meshConsolidationEnabled());
    }
    if (m_cameraSpeedSpin)
    {
        m_cameraSpeedSpin->setValue(m_viewportHost->cameraSpeed());
    }
    if (m_validationLayersCheck)
    {
        m_validationLayersCheck->setChecked(m_projectSession.currentValidationLayersEnabled());
    }
    if (m_validationRestartLabel)
    {
        m_validationRestartLabel->hide(); // Hide restart hint on initial load
    }
    if (m_freeFlyCameraCheck)
    {
        m_freeFlyCameraCheck->setChecked(m_projectSession.currentFreeFlyCameraEnabled());
    }
    
    m_updatingCameraSettings = false;
}

void MainWindowShell::applyCameraSettings()
{
    saveProjectState();
}

}  // namespace motive::ui
