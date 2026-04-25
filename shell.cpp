#include "shell.h"
#include "asset_browser_widget.h"
#include "host_widget.h"
#include "camera_follow_settings.h"
#include "transform_undo_command.h"
#include "physics_interface.h"

#include <glm/glm.hpp>

#include <QShortcut>
#include <QUndoView>

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTabWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

namespace motive::ui {
namespace {

QString editorDarkStyleSheet()
{
    return QStringLiteral(
        "QMainWindow, QWidget { background: #10161d; color: #edf2f7; }"
        "QLabel { color: #edf2f7; }"
        "QMenuBar { background: #10161d; color: #edf2f7; border-bottom: 1px solid #202934; }"
        "QMenuBar::item { background: transparent; padding: 4px 10px; }"
        "QMenuBar::item:selected { background: #233142; }"
        "QMenu { background: #10161d; color: #edf2f7; border: 1px solid #202934; }"
        "QMenu::item:selected { background: #233142; }"
        "QTabWidget::pane { background: #10161d; border: 1px solid #202934; border-radius: 10px; }"
        "QTabBar::tab { background: #1b2430; color: #edf2f7; padding: 8px 16px; margin-right: 2px; border: 1px solid #2e3b4a; border-bottom: none; border-top-left-radius: 8px; border-top-right-radius: 8px; }"
        "QTabBar::tab:selected { background: #233142; border-color: #3a4a5f; }"
        "QTabBar::tab:hover { background: #2a3749; }"
        "QTreeWidget, QTreeView, QListWidget { background: #0c1015; color: #edf2f7; border: 1px solid #202934; border-radius: 10px; }"
        "QTreeWidget::item, QTreeView::item { padding: 4px 8px; }"
        "QTreeWidget::item:selected, QTreeView::item:selected, QListWidget::item:selected { background: #233142; }"
        "QTreeWidget::item:hover, QTreeView::item:hover, QListWidget::item:hover { background: #1b2430; }"
        "QComboBox, QDoubleSpinBox, QCheckBox, QPushButton, QToolButton, QLineEdit { background: #1b2430; color: #edf2f7; border: 1px solid #2e3b4a; border-radius: 7px; padding: 4px 8px; }"
        "QComboBox:hover, QDoubleSpinBox:hover, QPushButton:hover, QToolButton:hover, QLineEdit:hover { background: #233142; }"
        "QComboBox::drop-down { border-left: 1px solid #2e3b4a; }"
        "QComboBox QAbstractItemView { background: #1b2430; color: #edf2f7; border: 1px solid #2e3b4a; selection-background-color: #233142; }"
        "QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { background: #2e3b4a; border: 1px solid #3a4a5f; }"
        "QDockWidget { color: #edf2f7; }"
        "QDockWidget::title { background: #10161d; padding: 6px 8px; border-bottom: 1px solid #202934; }"
        "QToolTip { background: #05080c; color: #edf2f7; border: 1px solid #24303c; }");
}

QJsonObject widgetMetrics(const QWidget* widget)
{
    QJsonObject object;
    if (!widget)
    {
        object.insert(QStringLiteral("exists"), false);
        return object;
    }

    const QSize sizeHint = widget->sizeHint();
    const QSize minimumSizeHint = widget->minimumSizeHint();
    const QSize minimumSize = widget->minimumSize();
    const QSize maximumSize = widget->maximumSize();
    const QSizePolicy sizePolicy = widget->sizePolicy();

    object.insert(QStringLiteral("exists"), true);
    object.insert(QStringLiteral("className"), QString::fromUtf8(widget->metaObject()->className()));
    object.insert(QStringLiteral("objectName"), widget->objectName());
    object.insert(QStringLiteral("visible"), widget->isVisible());
    object.insert(QStringLiteral("enabled"), widget->isEnabled());
    object.insert(QStringLiteral("x"), widget->x());
    object.insert(QStringLiteral("y"), widget->y());
    object.insert(QStringLiteral("width"), widget->width());
    object.insert(QStringLiteral("height"), widget->height());
    object.insert(QStringLiteral("minimumWidth"), minimumSize.width());
    object.insert(QStringLiteral("minimumHeight"), minimumSize.height());
    object.insert(QStringLiteral("maximumWidth"), maximumSize.width());
    object.insert(QStringLiteral("maximumHeight"), maximumSize.height());
    object.insert(QStringLiteral("sizeHintWidth"), sizeHint.width());
    object.insert(QStringLiteral("sizeHintHeight"), sizeHint.height());
    object.insert(QStringLiteral("minimumSizeHintWidth"), minimumSizeHint.width());
    object.insert(QStringLiteral("minimumSizeHintHeight"), minimumSizeHint.height());
    object.insert(QStringLiteral("sizePolicyHorizontal"), static_cast<int>(sizePolicy.horizontalPolicy()));
    object.insert(QStringLiteral("sizePolicyVertical"), static_cast<int>(sizePolicy.verticalPolicy()));
    object.insert(QStringLiteral("sizePolicyHorizontalStretch"), sizePolicy.horizontalStretch());
    object.insert(QStringLiteral("sizePolicyVerticalStretch"), sizePolicy.verticalStretch());
    return object;
}

QJsonObject splitterMetrics(const QSplitter* splitter)
{
    QJsonObject object = widgetMetrics(splitter);
    if (!splitter)
    {
        return object;
    }

    QJsonArray sizes;
    const QList<int> sizeValues = splitter->sizes();
    for (const int value : sizeValues)
    {
        sizes.append(value);
    }

    object.insert(QStringLiteral("orientation"),
                  splitter->orientation() == Qt::Horizontal ? QStringLiteral("horizontal")
                                                            : QStringLiteral("vertical"));
    object.insert(QStringLiteral("handleWidth"), splitter->handleWidth());
    object.insert(QStringLiteral("childrenCollapsible"), splitter->childrenCollapsible());
    object.insert(QStringLiteral("opaqueResize"), splitter->opaqueResize());
    object.insert(QStringLiteral("sizes"), sizes);
    return object;
}

}  // namespace

MainWindowShell::MainWindowShell(QWidget* parent)
    : QMainWindow(parent)
{
    resize(1600, 900);
    setStyleSheet(editorDarkStyleSheet());

    m_splitter = new QSplitter(this);
    m_leftPane = new QWidget(m_splitter);
    auto* leftLayout = new QVBoxLayout(m_leftPane);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);

    // Create a vertical splitter for adjustable boundary between file chooser and hierarchy
    auto* leftVerticalSplitter = new QSplitter(Qt::Vertical, m_leftPane);
    leftVerticalSplitter->setChildrenCollapsible(false);
    
    m_assetBrowser = new AssetBrowserWidget(leftVerticalSplitter);
    leftVerticalSplitter->addWidget(m_assetBrowser);
    
    // Create a container for hierarchy section
    auto* hierarchyContainer = new QWidget(leftVerticalSplitter);
    auto* hierarchyLayout = new QVBoxLayout(hierarchyContainer);
    hierarchyLayout->setContentsMargins(0, 0, 0, 0);
    hierarchyLayout->setSpacing(8);
    
    auto* hierarchyLabel = new QLabel(QStringLiteral("Hierarchy"), hierarchyContainer);
    QFont hierarchyLabelFont = hierarchyLabel->font();
    hierarchyLabelFont.setBold(true);
    hierarchyLabel->setFont(hierarchyLabelFont);
    hierarchyLabel->setContentsMargins(0, 4, 0, 4);
    hierarchyLayout->addWidget(hierarchyLabel);
    
    m_hierarchyTree = new QTreeWidget(hierarchyContainer);
    m_hierarchyTree->setColumnCount(1);
    m_hierarchyTree->setHeaderHidden(true);
    m_hierarchyTree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_hierarchyTree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_hierarchyTree->viewport()->installEventFilter(this);
    connect(m_hierarchyTree, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& pos)
    {
        QTreeWidgetItem* item = m_hierarchyTree->itemAt(pos);
        const int row = item ? item->data(0, Qt::UserRole).toInt() : -1;
        const int nodeType = item ? item->data(0, Qt::UserRole + 3).toInt() : -1;
        const int cameraIndex = item ? item->data(0, Qt::UserRole + 5).toInt() : -1;
        const QString cameraId = item ? item->data(0, Qt::UserRole + 6).toString() : QString();
        
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
        
        // Check if this is a camera node
        const bool isCamera = (nodeType == static_cast<int>(ViewportHostWidget::HierarchyNode::Type::Camera));
        
        if (isCamera)
        {
            // Camera context menu
            if (!m_viewportHost) return;
            
            QMenu menu(this);
            QAction* renameAction = menu.addAction(QStringLiteral("Rename"));
            QAction* deleteAction = menu.addAction(QStringLiteral("Delete"));
            
            QAction* chosen = menu.exec(m_hierarchyTree->mapToGlobal(pos));
            if (chosen == renameAction)
            {
                // Get current camera name
                QList<ViewportHostWidget::CameraConfig> configs = m_viewportHost->cameraConfigs();
                QString currentName;
                if (cameraIndex >= 0 && cameraIndex < configs.size())
                {
                    currentName = configs[cameraIndex].name;
                }
                else if (!cameraId.isEmpty())
                {
                    // Try to find camera by ID
                    for (int i = 0; i < configs.size(); ++i)
                    {
                        if (configs[i].id == cameraId)
                        {
                            currentName = configs[i].name;
                            break;
                        }
                    }
                }
                
                bool ok = false;
                const QString name = QInputDialog::getText(this,
                                                           QStringLiteral("Rename Camera"),
                                                           QStringLiteral("Name:"),
                                                           QLineEdit::Normal,
                                                           currentName,
                                                           &ok);
                if (ok && !name.trimmed().isEmpty())
                {
                    if (cameraIndex >= 0 && cameraIndex < configs.size())
                    {
                        configs[cameraIndex].name = name;
                        m_viewportHost->updateCameraConfig(cameraIndex, configs[cameraIndex]);
                    }
                    else if (!cameraId.isEmpty())
                    {
                        // Find camera by ID and update
                        for (int i = 0; i < configs.size(); ++i)
                        {
                            if (configs[i].id == cameraId)
                            {
                                configs[i].name = name;
                                m_viewportHost->updateCameraConfig(i, configs[i]);
                                break;
                            }
                        }
                    }
                    refreshHierarchy(m_viewportHost->sceneItems());
                    saveProjectState();
                }
            }
            else if (chosen == deleteAction)
            {
                if (cameraIndex >= 0)
                {
                    m_viewportHost->deleteCamera(cameraIndex);
                }
                else if (!cameraId.isEmpty())
                {
                    // Find camera by ID and delete
                    QList<ViewportHostWidget::CameraConfig> configs = m_viewportHost->cameraConfigs();
                    for (int i = 0; i < configs.size(); ++i)
                    {
                        if (configs[i].id == cameraId)
                        {
                            m_viewportHost->deleteCamera(i);
                            break;
                        }
                    }
                }
                refreshHierarchy(m_viewportHost->sceneItems());
                saveProjectState();
            }
            return;
        }
        
        // Regular scene item (model) context menu
        if (row < 0 || row >= m_sceneItems.size() || !m_viewportHost)
        {
            return;
        }
        QMenu menu(this);
        QAction* renameAction = menu.addAction(QStringLiteral("Rename"));
        QAction* focusAction = menu.addAction(QStringLiteral("Focus"));
        QAction* setFocusPointAction = menu.addAction(QStringLiteral("Set Focus Point"));
        QAction* relocateAction = menu.addAction(QStringLiteral("Relocate 5m in front of camera"));
        
        // Add Follow submenu
        QMenu* followMenu = menu.addMenu(QStringLiteral("Follow"));
        QAction* createFollowAction = followMenu->addAction(QStringLiteral("Create Follow Cam"));
        QAction* configureFollowAction = followMenu->addAction(QStringLiteral("Configure..."));
        QAction* deleteFollowAction = followMenu->addAction(QStringLiteral("Delete Follow Cam"));
        
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
        else if (chosen == setFocusPointAction)
        {
            m_viewportHost->captureSceneItemFocusFromCurrentCamera(row);
            updateInspectorForSelection(m_hierarchyTree->currentItem());
        }
        else if (chosen == relocateAction)
        {
            m_viewportHost->relocateSceneItemInFrontOfCamera(row);
            updateInspectorForSelection(m_hierarchyTree->currentItem());
        }
        else if (chosen == createFollowAction)
        {
            // Create follow camera with default settings
            int cameraIndex = m_viewportHost->createFollowCamera(row, 5.0f, 0.0f, 20.0f);
            if (cameraIndex >= 0)
            {
                // Refresh hierarchy to show the new follow camera
                refreshHierarchy(m_viewportHost->sceneItems());
                
                // Save project state
                saveProjectState();
            }
        }
        else if (chosen == configureFollowAction)
        {
            // Find the follow camera for this scene item
            QList<ViewportHostWidget::CameraConfig> configs = m_viewportHost->cameraConfigs();
            int followCamIndex = -1;
            for (int i = 0; i < configs.size(); ++i) {
                if (configs[i].isFollowCamera() && configs[i].followTargetIndex == row) {
                    followCamIndex = i;
                    break;
                }
            }
            
            if (followCamIndex < 0) {
                // No follow camera exists yet - create one first
                followCamIndex = m_viewportHost->createFollowCamera(row, 5.0f, 0.0f, 20.0f);
                if (followCamIndex < 0) return;
                configs = m_viewportHost->cameraConfigs();
            }
            
            // Show dialog to configure follow settings (only distance and angle as requested)
            QDialog dialog(this);
            dialog.setWindowTitle(QStringLiteral("Follow Camera Settings"));
            auto* layout = new QVBoxLayout(&dialog);
            auto* formLayout = new QFormLayout();
            
            auto* distSpin = new QDoubleSpinBox(&dialog);
            distSpin->setRange(followcam::kMinDistance, 100.0);
            distSpin->setValue(configs[followCamIndex].followDistance);
            distSpin->setSingleStep(0.5);
            distSpin->setDecimals(2);
            formLayout->addRow(QStringLiteral("Distance:"), distSpin);
            
            auto* yawSpin = new QDoubleSpinBox(&dialog);
            yawSpin->setRange(-180.0, 180.0);
            yawSpin->setValue(configs[followCamIndex].followYaw);
            yawSpin->setSuffix(QStringLiteral("°"));
            yawSpin->setDecimals(1);
            formLayout->addRow(QStringLiteral("Horizontal Angle:"), yawSpin);
            
            auto* pitchSpin = new QDoubleSpinBox(&dialog);
            pitchSpin->setRange(-89.0, 89.0);
            pitchSpin->setValue(configs[followCamIndex].followPitch);
            pitchSpin->setSuffix(QStringLiteral("°"));
            pitchSpin->setDecimals(1);
            formLayout->addRow(QStringLiteral("Vertical Angle:"), pitchSpin);
            
            layout->addLayout(formLayout);
            
            auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
            layout->addWidget(buttonBox);
            
            connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            
            if (dialog.exec() == QDialog::Accepted)
            {
                // Update the camera config
                configs[followCamIndex].followDistance = static_cast<float>(distSpin->value());
                configs[followCamIndex].followYaw = static_cast<float>(yawSpin->value());
                configs[followCamIndex].followPitch = static_cast<float>(pitchSpin->value());
                
                m_viewportHost->updateCameraConfig(followCamIndex, configs[followCamIndex]);
                
                // Save project state
                saveProjectState();
            }
        }
        else if (chosen == deleteFollowAction)
        {
            // Find and delete the follow camera for this scene item
            QList<ViewportHostWidget::CameraConfig> configs = m_viewportHost->cameraConfigs();
            for (int i = 0; i < configs.size(); ++i) {
                if (configs[i].isFollowCamera() && configs[i].followTargetIndex == row) {
                    m_viewportHost->deleteCamera(i);
                    refreshHierarchy(m_viewportHost->sceneItems());
                    saveProjectState();
                    break;
                }
            }
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
    hierarchyLayout->addWidget(m_hierarchyTree, 1);
    
    leftVerticalSplitter->addWidget(hierarchyContainer);
    leftVerticalSplitter->setStretchFactor(0, 3); // File chooser gets more space initially
    leftVerticalSplitter->setStretchFactor(1, 1); // Hierarchy gets less space initially
    
    leftLayout->addWidget(leftVerticalSplitter, 1);

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
        if (m_hierarchyTree && m_hierarchyTree->currentItem())
        {
            int row = m_hierarchyTree->currentItem()->data(0, Qt::UserRole).toInt();
            // Handle Camera and Follow Camera entries (any row <= kHierarchyCameraIndex)
            if (row <= MainWindowShell::kHierarchyCameraIndex)
            {
                updateInspectorForSelection(m_hierarchyTree->currentItem());
            }
        }
    });
    m_viewportHost->setViewportFocusChangedCallback([this](const QString& cameraId)
    {
        if (!m_hierarchyTree || cameraId.isEmpty())
        {
            return;
        }

        QList<QTreeWidgetItem*> items = m_hierarchyTree->findItems(QStringLiteral("*"), Qt::MatchWildcard | Qt::MatchRecursive);
        QTreeWidgetItem* targetItem = nullptr;
        for (QTreeWidgetItem* item : items)
        {
            if (!item)
            {
                continue;
            }
            const int type = item->data(0, Qt::UserRole + 3).toInt();
            if (type != static_cast<int>(ViewportHostWidget::HierarchyNode::Type::Camera))
            {
                continue;
            }
            if (item->data(0, Qt::UserRole + 6).toString() == cameraId)
            {
                targetItem = item;
                break;
            }
        }

        if (!targetItem)
        {
            return;
        }

        if (m_hierarchyTree->currentItem() != targetItem)
        {
            QSignalBlocker blocker(m_hierarchyTree);
            m_hierarchyTree->setCurrentItem(targetItem);
        }
        updateInspectorForSelection(targetItem);
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
    inspectorDock->setFeatures(QDockWidget::DockWidgetMovable);
    m_rightTabs = new QTabWidget(inspectorDock);
    auto* inspectorPanel = new QWidget(m_rightTabs);
    auto* inspectorLayout = new QVBoxLayout(inspectorPanel);
    inspectorLayout->setContentsMargins(8, 8, 8, 8);
    inspectorLayout->setSpacing(8);
    m_inspectorNameValue = new QLabel(QStringLiteral("-"), inspectorPanel);
    m_inspectorPathValue = new QLabel(QStringLiteral("-"), inspectorPanel);
    m_inspectorPathValue->setWordWrap(true);
    m_animationModeValue = new QLabel(QStringLiteral("-"), inspectorPanel);
    m_cameraTypeValue = new QLabel(QStringLiteral("-"), inspectorPanel);
    m_boundsSizeValue = new QLabel(QStringLiteral("-"), inspectorPanel);
    m_inspectorTexturePreview = new QLabel(QStringLiteral("No texture"), inspectorPanel);
    m_inspectorTexturePreview->setMinimumSize(96, 96);
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
    m_paintColorContainer = new QWidget(inspectorPanel);
    auto* paintColorLayout = new QHBoxLayout(m_paintColorContainer);
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
    
    // Animation-Physics Coupling dropdown
    m_animationPhysicsCouplingCombo = new QComboBox(m_animationControlsWidget);
    m_animationPhysicsCouplingCombo->addItem(QStringLiteral("Animation Only"), QStringLiteral("AnimationOnly"));
    m_animationPhysicsCouplingCombo->addItem(QStringLiteral("Kinematic"), QStringLiteral("Kinematic"));
    m_animationPhysicsCouplingCombo->addItem(QStringLiteral("Root Motion + Physics"), QStringLiteral("RootMotionPhysics"));
    m_animationPhysicsCouplingCombo->addItem(QStringLiteral("Physics Driven"), QStringLiteral("PhysicsDriven"));
    m_animationPhysicsCouplingCombo->addItem(QStringLiteral("Ragdoll"), QStringLiteral("Ragdoll"));
    m_animationPhysicsCouplingCombo->addItem(QStringLiteral("Partial Ragdoll"), QStringLiteral("PartialRagdoll"));
    m_animationPhysicsCouplingCombo->addItem(QStringLiteral("Active Ragdoll"), QStringLiteral("ActiveRagdoll"));
    
    animationControlsLayout->addWidget(m_animationClipCombo);
    animationControlsLayout->addWidget(animationFlagsWidget);
    animationControlsLayout->addWidget(animationSpeedWidget);
    animationControlsLayout->addWidget(new QLabel(QStringLiteral("Physics Coupling:"), m_animationControlsWidget));
    animationControlsLayout->addWidget(m_animationPhysicsCouplingCombo);

    m_lightTypeCombo = new QComboBox(inspectorPanel);
    m_lightTypeCombo->addItem(QStringLiteral("Directional"), QStringLiteral("directional"));
    m_lightTypeCombo->addItem(QStringLiteral("Ambient"), QStringLiteral("ambient"));
    m_lightTypeCombo->addItem(QStringLiteral("Hemispherical"), QStringLiteral("hemispherical"));
    m_lightBrightnessSpin = createSpinBox(inspectorPanel, 0.0, 100.0, 0.01);
    m_lightColorWidget = new QWidget(inspectorPanel);
    m_lightColorWidget->setFixedSize(60, 24);
    m_lightColorWidget->setStyleSheet(QStringLiteral("background-color: #ffffff; border: 1px solid #888;"));
    auto* lightColorButton = new QPushButton(QStringLiteral("Change"), inspectorPanel);
    m_lightColorContainer = new QWidget(inspectorPanel);
    auto* lightColorLayout = new QHBoxLayout(m_lightColorContainer);
    lightColorLayout->setContentsMargins(0, 0, 0, 0);
    lightColorLayout->addWidget(m_lightColorWidget);
    lightColorLayout->addWidget(lightColorButton);
    lightColorLayout->addStretch(1);

    // Create follow target selector for cameras
    m_followTargetCombo = new QComboBox(inspectorPanel);
    m_followTargetCombo->addItem(QStringLiteral("None (Free Camera)"), -1);
    m_followTargetLabel = new QLabel(inspectorPanel);
    
    // Free fly camera toggle
    m_wasdRoutingCombo = new QComboBox(inspectorPanel);
    m_wasdRoutingCombo->addItem(QStringLiteral("Camera Move"), QStringLiteral("FreeFly"));
    m_wasdRoutingCombo->addItem(QStringLiteral("Character Passthrough"), QStringLiteral("CharacterFollow"));
    m_wasdRoutingCombo->addItem(QStringLiteral("Orbit Passthrough"), QStringLiteral("OrbitFollow"));
    m_wasdRoutingCombo->addItem(QStringLiteral("Disabled"), QStringLiteral("Fixed"));
    m_wasdRoutingCombo->setToolTip(
        QStringLiteral("Explicit WASD routing mode: camera move, character passthrough, orbit passthrough, or disabled."));
    m_freeFlyCameraCheck = new QCheckBox(QStringLiteral("Free fly mode (WASD moves camera)"), inspectorPanel);
    m_freeFlyCameraCheck->setChecked(true);
    m_freeFlyCameraCheck->setToolTip(QStringLiteral("When enabled, WASD moves the camera. When disabled, WASD controls the character and right-drag orbits."));
    m_invertHorizontalDragCheck = new QCheckBox(QStringLiteral("Invert horizontal right-drag"), inspectorPanel);
    m_invertHorizontalDragCheck->setChecked(false);
    m_invertHorizontalDragCheck->setToolTip(QStringLiteral("Invert left/right camera rotation while right-dragging."));
    
    // Near/Far clipping planes
    m_nearClipSpin = createSpinBox(inspectorPanel, 0.001, 100.0, 0.01);
    m_nearClipSpin->setValue(0.1);
    m_nearClipSpin->setToolTip(QStringLiteral("Near clipping plane distance"));
    m_farClipSpin = createSpinBox(inspectorPanel, 0.1, 0.0, 0.1);
    m_farClipSpin->setValue(100.0);
    m_farClipSpin->setToolTip(QStringLiteral("Far clipping plane distance"));
    
    // Create follow camera parameter controls
    m_followParamsLabel = new QLabel(QStringLiteral("Follow Parameters"), inspectorPanel);
    m_followDistanceSpin = createSpinBox(inspectorPanel, followcam::kMinDistance, 100.0, 0.1);
    m_followDistanceSpin->setValue(5.0);
    m_followYawSpin = createSpinBox(inspectorPanel, -360.0, 360.0, 1.0);
    m_followYawSpin->setValue(0.0);
    m_followPitchSpin = createSpinBox(inspectorPanel, -90.0, 90.0, 1.0);
    m_followPitchSpin->setValue(20.0);
    m_followSmoothSpin = createSpinBox(inspectorPanel, 0.0, 50.0, 0.1);
    m_followSmoothSpin->setValue(followcam::kDefaultSmoothSpeed);
    m_followSmoothSpin->setToolTip(QStringLiteral("Follow-camera response speed. 0 disables smoothing; higher values track target motion more tightly."));

    // Create translation spin boxes
    m_translationWidget = new QWidget(inspectorPanel);
    auto* translationLayout = new QHBoxLayout(m_translationWidget);
    m_inspectorTranslationX = createSpinBox(inspectorPanel, 0.0, 0.0, 0.001);
    m_inspectorTranslationY = createSpinBox(inspectorPanel, 0.0, 0.0, 0.001);
    m_inspectorTranslationZ = createSpinBox(inspectorPanel, 0.0, 0.0, 0.001);
    translationLayout->addWidget(new QLabel("X:", m_translationWidget));
    translationLayout->addWidget(m_inspectorTranslationX);
    translationLayout->addWidget(new QLabel("Y:", m_translationWidget));
    translationLayout->addWidget(m_inspectorTranslationY);
    translationLayout->addWidget(new QLabel("Z:", m_translationWidget));
    translationLayout->addWidget(m_inspectorTranslationZ);
    translationLayout->setContentsMargins(0, 0, 0, 0);

    // Create rotation spin boxes
    m_rotationWidget = new QWidget(inspectorPanel);
    auto* rotationLayout = new QHBoxLayout(m_rotationWidget);
    m_inspectorRotationX = createSpinBox(inspectorPanel, 0.0, 0.0, 0.1);
    m_inspectorRotationY = createSpinBox(inspectorPanel, 0.0, 0.0, 0.1);
    m_inspectorRotationZ = createSpinBox(inspectorPanel, 0.0, 0.0, 0.1);
    rotationLayout->addWidget(new QLabel("X:", m_rotationWidget));
    rotationLayout->addWidget(m_inspectorRotationX);
    rotationLayout->addWidget(new QLabel("Y:", m_rotationWidget));
    rotationLayout->addWidget(m_inspectorRotationY);
    rotationLayout->addWidget(new QLabel("Z:", m_rotationWidget));
    rotationLayout->addWidget(m_inspectorRotationZ);
    rotationLayout->setContentsMargins(0, 0, 0, 0);

    // Create scale spin boxes
    m_scaleWidget = new QWidget(inspectorPanel);
    auto* scaleLayout = new QHBoxLayout(m_scaleWidget);
    m_inspectorScaleX = createSpinBox(inspectorPanel, 0.001, 0.0, 0.001);
    m_inspectorScaleY = createSpinBox(inspectorPanel, 0.001, 0.0, 0.001);
    m_inspectorScaleZ = createSpinBox(inspectorPanel, 0.001, 0.0, 0.001);
    m_lockScaleXYZCheck = new QCheckBox(QStringLiteral("Lock XYZ"), m_scaleWidget);
    m_lockScaleXYZCheck->setChecked(true);
    m_lockScaleXYZCheck->setToolTip(QStringLiteral("When enabled, editing any scale axis applies the same value to X/Y/Z."));
    scaleLayout->addWidget(new QLabel("X:", m_scaleWidget));
    scaleLayout->addWidget(m_inspectorScaleX);
    scaleLayout->addWidget(new QLabel("Y:", m_scaleWidget));
    scaleLayout->addWidget(m_inspectorScaleY);
    scaleLayout->addWidget(new QLabel("Z:", m_scaleWidget));
    scaleLayout->addWidget(m_inspectorScaleZ);
    scaleLayout->addWidget(m_lockScaleXYZCheck);
    scaleLayout->setContentsMargins(0, 0, 0, 0);
    
    // Per-object Gravity controls
    m_elementUseGravityCheck = new QCheckBox(QStringLiteral("Use World Gravity"), inspectorPanel);
    m_elementUseGravityCheck->setChecked(true);
    m_elementGravityWidget = new QWidget(inspectorPanel);
    auto* elementGravityLayout = new QHBoxLayout(m_elementGravityWidget);
    m_elementGravityX = createSpinBox(inspectorPanel, -50.0, 50.0, 0.1);
    m_elementGravityY = createSpinBox(inspectorPanel, -50.0, 50.0, 0.1);
    m_elementGravityZ = createSpinBox(inspectorPanel, -50.0, 50.0, 0.1);
    elementGravityLayout->addWidget(new QLabel("X:", m_elementGravityWidget));
    elementGravityLayout->addWidget(m_elementGravityX);
    elementGravityLayout->addWidget(new QLabel("Y:", m_elementGravityWidget));
    elementGravityLayout->addWidget(m_elementGravityY);
    elementGravityLayout->addWidget(new QLabel("Z:", m_elementGravityWidget));
    elementGravityLayout->addWidget(m_elementGravityZ);
    elementGravityLayout->setContentsMargins(0, 0, 0, 0);
    m_characterTurnResponsivenessSpin = createSpinBox(inspectorPanel, 0.1, 50.0, 0.1);
    m_characterTurnResponsivenessSpin->setValue(10.0);
    m_characterTurnResponsivenessSpin->setToolTip(QStringLiteral("How quickly this target rotates to face movement direction."));
    m_objectFollowCamInfoValue = new QLabel(QStringLiteral("-"), inspectorPanel);
    m_objectFollowCamInfoValue->setWordWrap(true);
    m_objectKinematicInfoValue = new QLabel(QStringLiteral("-"), inspectorPanel);
    m_objectKinematicInfoValue->setWordWrap(true);
    m_objectAnimationRuntimeInfoValue = new QLabel(QStringLiteral("-"), inspectorPanel);
    m_objectAnimationRuntimeInfoValue->setWordWrap(true);
    m_motionOverlayEnabledCheck = new QCheckBox(QStringLiteral("Enable Motion Gizmos Overlay"), inspectorPanel);
    m_motionOverlayTargetMarkersCheck = new QCheckBox(QStringLiteral("Show Target Markers"), inspectorPanel);
    m_motionOverlayVelocityCheck = new QCheckBox(QStringLiteral("Show Velocity Vector"), inspectorPanel);
    m_motionOverlayCameraLineCheck = new QCheckBox(QStringLiteral("Show Camera→Target Line"), inspectorPanel);
    m_motionOverlayCenterCrosshairCheck = new QCheckBox(QStringLiteral("Show Screen Center Crosshair"), inspectorPanel);
    m_motionOverlayVelocityScaleSpin = createSpinBox(inspectorPanel, 0.01, 10.0, 0.01);
    m_motionOverlayVelocityScaleSpin->setValue(0.25);
    m_motionOverlayVelocityScaleSpin->setToolTip(QStringLiteral("Scale applied to velocity vector gizmo length."));

    m_elementDetailTabs = new QTabWidget(inspectorPanel);
    m_elementDetailTabs->setDocumentMode(true);
    m_elementDetailTabs->setTabPosition(QTabWidget::North);

    auto* overviewTab = new QWidget(m_elementDetailTabs);
    auto* overviewLayout = new QVBoxLayout(overviewTab);
    overviewLayout->setContentsMargins(6, 6, 6, 6);
    overviewLayout->setSpacing(8);

    auto* visualTab = new QWidget(m_elementDetailTabs);
    auto* visualLayout = new QVBoxLayout(visualTab);
    visualLayout->setContentsMargins(6, 6, 6, 6);
    visualLayout->setSpacing(8);

    auto* motionTab = new QWidget(m_elementDetailTabs);
    auto* motionLayout = new QVBoxLayout(motionTab);
    motionLayout->setContentsMargins(6, 6, 6, 6);
    motionLayout->setSpacing(8);

    auto* cameraTab = new QWidget(m_elementDetailTabs);
    auto* cameraTabLayout = new QVBoxLayout(cameraTab);
    cameraTabLayout->setContentsMargins(6, 6, 6, 6);
    cameraTabLayout->setSpacing(8);

    auto* runtimeTab = new QWidget(m_elementDetailTabs);
    auto* runtimeTabLayout = new QVBoxLayout(runtimeTab);
    runtimeTabLayout->setContentsMargins(6, 6, 6, 6);
    runtimeTabLayout->setSpacing(8);

    m_summarySection = new QGroupBox(QStringLiteral("Summary"), overviewTab);
    auto* summaryLayout = new QFormLayout(m_summarySection);
    summaryLayout->addRow(QStringLiteral("Name"), m_inspectorNameValue);
    summaryLayout->addRow(QStringLiteral("Source"), m_inspectorPathValue);
    summaryLayout->addRow(QStringLiteral("Animation Path"), m_animationModeValue);
    summaryLayout->addRow(QStringLiteral("Bounds Size"), m_boundsSizeValue);
    summaryLayout->addRow(QStringLiteral("Texture"), m_inspectorTexturePreview);

    m_materialSection = new QGroupBox(QStringLiteral("Material & Mesh"), visualTab);
    auto* materialLayout = new QFormLayout(m_materialSection);
    materialLayout->addRow(QStringLiteral("Load"), m_loadMeshConsolidationCheck);
    materialLayout->addRow(QStringLiteral("Cull Mode"), m_primitiveCullModeCombo);
    materialLayout->addRow(QStringLiteral("Opacity"), m_primitiveForceAlphaButton);
    materialLayout->addRow(QStringLiteral("Paint Override"), m_paintOverrideCheck);
    materialLayout->addRow(QStringLiteral("Paint Color"), m_paintColorContainer);

    m_animationSection = new QGroupBox(QStringLiteral("Animation"), motionTab);
    auto* animationLayout = new QFormLayout(m_animationSection);
    animationLayout->addRow(QStringLiteral("Controls"), m_animationControlsWidget);

    m_cameraSection = new QGroupBox(QStringLiteral("Camera"), cameraTab);
    auto* cameraLayout = new QFormLayout(m_cameraSection);
    cameraLayout->addRow(QStringLiteral("Camera Type"), m_cameraTypeValue);
    cameraLayout->addRow(QStringLiteral("WASD Routing"), m_wasdRoutingCombo);
    cameraLayout->addRow(QStringLiteral("Drag"), m_invertHorizontalDragCheck);
    cameraLayout->addRow(QStringLiteral("Near Clip"), m_nearClipSpin);
    cameraLayout->addRow(QStringLiteral("Far Clip"), m_farClipSpin);
    cameraLayout->addRow(QStringLiteral("Follow Target"), m_followTargetCombo);
    cameraLayout->addRow(m_followParamsLabel);
    cameraLayout->addRow(QStringLiteral("Distance"), m_followDistanceSpin);
    cameraLayout->addRow(QStringLiteral("Yaw"), m_followYawSpin);
    cameraLayout->addRow(QStringLiteral("Pitch"), m_followPitchSpin);
    cameraLayout->addRow(QStringLiteral("Follow Damping"), m_followSmoothSpin);

    m_lightSection = new QGroupBox(QStringLiteral("Lighting"), visualTab);
    auto* lightLayout = new QFormLayout(m_lightSection);
    lightLayout->addRow(QStringLiteral("Light Type"), m_lightTypeCombo);
    lightLayout->addRow(QStringLiteral("Brightness"), m_lightBrightnessSpin);
    lightLayout->addRow(QStringLiteral("Color"), m_lightColorContainer);

    m_transformSection = new QGroupBox(QStringLiteral("Transform"), overviewTab);
    auto* transformLayout = new QFormLayout(m_transformSection);
    transformLayout->addRow(QStringLiteral("Translation"), m_translationWidget);
    transformLayout->addRow(QStringLiteral("Rotation"), m_rotationWidget);
    transformLayout->addRow(QStringLiteral("Scale"), m_scaleWidget);

    m_physicsSection = new QGroupBox(QStringLiteral("Physics & Motion"), motionTab);
    auto* physicsLayout = new QFormLayout(m_physicsSection);
    physicsLayout->addRow(QStringLiteral("WASD Enablement"), m_freeFlyCameraCheck);
    physicsLayout->addRow(QStringLiteral("Physics Gravity"), m_elementUseGravityCheck);
    physicsLayout->addRow(QStringLiteral("Custom Gravity"), m_elementGravityWidget);
    physicsLayout->addRow(QStringLiteral("Turn Responsiveness"), m_characterTurnResponsivenessSpin);

    m_runtimeSection = new QGroupBox(QStringLiteral("Runtime Diagnostics"), runtimeTab);
    auto* runtimeLayout = new QFormLayout(m_runtimeSection);
    runtimeLayout->addRow(QStringLiteral("Object Follow Cam"), m_objectFollowCamInfoValue);
    runtimeLayout->addRow(QStringLiteral("Kinematic Runtime"), m_objectKinematicInfoValue);
    runtimeLayout->addRow(QStringLiteral("Animation Runtime"), m_objectAnimationRuntimeInfoValue);

    m_motionDebugOverlaySection = new QGroupBox(QStringLiteral("Motion Debug Overlay"), runtimeTab);
    auto* motionOverlayLayout = new QFormLayout(m_motionDebugOverlaySection);
    motionOverlayLayout->addRow(m_motionOverlayEnabledCheck);
    motionOverlayLayout->addRow(m_motionOverlayTargetMarkersCheck);
    motionOverlayLayout->addRow(m_motionOverlayVelocityCheck);
    motionOverlayLayout->addRow(m_motionOverlayCameraLineCheck);
    motionOverlayLayout->addRow(m_motionOverlayCenterCrosshairCheck);
    motionOverlayLayout->addRow(QStringLiteral("Velocity Scale"), m_motionOverlayVelocityScaleSpin);

    overviewLayout->addWidget(m_summarySection);
    overviewLayout->addWidget(m_transformSection);
    overviewLayout->addStretch(1);

    visualLayout->addWidget(m_materialSection);
    visualLayout->addWidget(m_lightSection);
    visualLayout->addStretch(1);

    motionLayout->addWidget(m_animationSection);
    motionLayout->addWidget(m_physicsSection);
    motionLayout->addStretch(1);

    cameraTabLayout->addWidget(m_cameraSection);
    cameraTabLayout->addStretch(1);

    runtimeTabLayout->addWidget(m_runtimeSection);
    runtimeTabLayout->addWidget(m_motionDebugOverlaySection);
    runtimeTabLayout->addStretch(1);

    m_elementDetailTabs->addTab(overviewTab, QStringLiteral("Overview"));
    m_elementDetailTabs->addTab(visualTab, QStringLiteral("Visual"));
    m_elementDetailTabs->addTab(motionTab, QStringLiteral("Motion"));
    m_elementDetailTabs->addTab(cameraTab, QStringLiteral("Camera"));
    m_elementDetailTabs->addTab(runtimeTab, QStringLiteral("Runtime"));

    inspectorLayout->addWidget(m_elementDetailTabs);
    inspectorLayout->addStretch(1);
    m_rightTabs->addTab(wrapTabInScrollArea(inspectorPanel), QStringLiteral("Element"));
    inspectorDock->setWidget(m_rightTabs);
    addDockWidget(Qt::RightDockWidgetArea, inspectorDock);

    setupCameraSettingsPanel();

    auto resolveCameraIndexFromTreeItem = [this](QTreeWidgetItem* item) -> int
    {
        if (!item || !m_viewportHost)
        {
            return -1;
        }

        const QString cameraId = item->data(0, Qt::UserRole + 6).toString();
        if (!cameraId.isEmpty())
        {
            const int idIndex = m_viewportHost->cameraIndexForId(cameraId);
            if (idIndex >= 0)
            {
                return idIndex;
            }
        }

        return item->data(0, Qt::UserRole + 5).toInt();
    };
    auto findFollowCameraIndexForScene = [this](int sceneIndex) -> int
    {
        if (!m_viewportHost || sceneIndex < 0)
        {
            return -1;
        }
        const auto configs = m_viewportHost->cameraConfigs();
        for (int i = 0; i < configs.size(); ++i)
        {
            if (configs[i].isFollowCamera() && configs[i].followTargetIndex == sceneIndex)
            {
                return i;
            }
        }
        return -1;
    };

    connect(m_hierarchyTree, &QTreeWidget::currentItemChanged, this, [this, resolveCameraIndexFromTreeItem](QTreeWidgetItem* current)
    {
        const bool suppressSelectionEffects =
            m_suppressHierarchySelectionEffects ||
            (QApplication::mouseButtons() & Qt::RightButton);

        // Check if a camera node was selected and switch to it
        if (current && m_viewportHost && !suppressSelectionEffects)
        {
            const int type = current->data(0, Qt::UserRole + 3).toInt();
            if (type == static_cast<int>(ViewportHostWidget::HierarchyNode::Type::Camera))
            {
                const int cameraIndex = resolveCameraIndexFromTreeItem(current);
                if (cameraIndex >= 0)
                {
                    // Selecting a camera in the hierarchy should show that camera in the focused viewport.
                    const auto configs = m_viewportHost->cameraConfigs();
                    if (cameraIndex < configs.size() && !configs[cameraIndex].id.isEmpty())
                    {
                        ViewportHostWidget::ViewportLayout layout = m_viewportHost->viewportLayout();
                        const int focusedIndex = m_viewportHost->focusedViewportIndex();
                        if (focusedIndex >= 0 &&
                            focusedIndex < layout.count &&
                            focusedIndex < layout.cameraIds.size() &&
                            layout.cameraIds[focusedIndex] != configs[cameraIndex].id)
                        {
                            layout.cameraIds[focusedIndex] = configs[cameraIndex].id;
                            m_viewportHost->setViewportLayout(layout);
                        }
                    }
                    m_viewportHost->setActiveCamera(cameraIndex);
                }
            }
            else if (type == static_cast<int>(ViewportHostWidget::HierarchyNode::Type::SceneItem))
            {
                const int sceneIndex = current->data(0, Qt::UserRole).toInt();
                if (sceneIndex >= 0)
                {
                    bool focusedViewportIsFreeFly = false;
                    const QString focusedCameraId = m_viewportHost->focusedViewportCameraId();
                    const auto configs = m_viewportHost->cameraConfigs();
                    for (const auto& config : configs)
                    {
                        if (config.id == focusedCameraId)
                        {
                            focusedViewportIsFreeFly =
                                config.freeFly || config.mode.compare(QStringLiteral("FreeFly"), Qt::CaseInsensitive) == 0;
                            break;
                        }
                    }

                    // Scene-item selection reframes only in fly mode.
                    if (focusedViewportIsFreeFly)
                    {
                        m_viewportHost->focusSceneItem(sceneIndex);
                    }
                }
            }
        }
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
        if (m_animationPhysicsCouplingCombo) {
            m_sceneItems[row].animationPhysicsCoupling = m_animationPhysicsCouplingCombo->currentData().toString();
            m_viewportHost->updateSceneItemAnimationPhysicsCoupling(row, m_animationPhysicsCouplingCombo->currentData().toString());
        }
        saveProjectState();
    };
    connect(m_animationClipCombo, &QComboBox::currentIndexChanged, this, [applyAnimationInspector]() { applyAnimationInspector(); });
    connect(m_animationPlayingCheck, &QCheckBox::toggled, this, [applyAnimationInspector](bool) { applyAnimationInspector(); });
    connect(m_animationLoopCheck, &QCheckBox::toggled, this, [applyAnimationInspector](bool) { applyAnimationInspector(); });
    connect(m_animationSpeedSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [applyAnimationInspector](double) { applyAnimationInspector(); });
    connect(m_animationPhysicsCouplingCombo, &QComboBox::currentIndexChanged, this, [applyAnimationInspector]() { applyAnimationInspector(); });
    
    // Connect per-object gravity controls
    auto applyGravityInspector = [this]() {
        if (m_updatingInspector || !m_viewportHost || !m_hierarchyTree) return;
        QTreeWidgetItem* current = m_hierarchyTree->currentItem();
        const int row = current ? current->data(0, Qt::UserRole).toInt() : -1;
        if (row < 0 || row >= m_sceneItems.size() || !m_elementUseGravityCheck) return;
        
        m_sceneItems[row].useGravity = m_elementUseGravityCheck->isChecked();
        m_sceneItems[row].customGravity = QVector3D(
            static_cast<float>(m_elementGravityX ? m_elementGravityX->value() : 0.0),
            static_cast<float>(m_elementGravityY ? m_elementGravityY->value() : 0.0),
            static_cast<float>(m_elementGravityZ ? m_elementGravityZ->value() : 0.0)
        );
        
        // Update physics through viewport host
        m_viewportHost->updateSceneItemPhysicsGravity(row, m_sceneItems[row].useGravity, m_sceneItems[row].customGravity);
        
        saveProjectState();
    };
    connect(m_elementUseGravityCheck, &QCheckBox::toggled, this, [applyGravityInspector](bool) { applyGravityInspector(); });
    connect(m_elementGravityX, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [applyGravityInspector](double) { applyGravityInspector(); });
    connect(m_elementGravityY, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [applyGravityInspector](double) { applyGravityInspector(); });
    connect(m_elementGravityZ, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [applyGravityInspector](double) { applyGravityInspector(); });

    auto applyCharacterMotionInspector = [this]() {
        if (m_updatingInspector || !m_viewportHost || !m_hierarchyTree || !m_characterTurnResponsivenessSpin) return;
        QTreeWidgetItem* current = m_hierarchyTree->currentItem();
        const int row = current ? current->data(0, Qt::UserRole).toInt() : -1;
        if (row < 0 || row >= m_sceneItems.size()) return;

        const float responsiveness = static_cast<float>(m_characterTurnResponsivenessSpin->value());
        m_sceneItems[row].characterTurnResponsiveness = responsiveness;
        m_viewportHost->updateSceneItemCharacterTurnResponsiveness(row, responsiveness);
        saveProjectState();
    };
    connect(m_characterTurnResponsivenessSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [applyCharacterMotionInspector](double) { applyCharacterMotionInspector(); });

    auto applyMotionOverlayInspector = [this]() {
        if (m_updatingInspector || !m_viewportHost ||
            !m_motionOverlayEnabledCheck ||
            !m_motionOverlayTargetMarkersCheck ||
            !m_motionOverlayVelocityCheck ||
            !m_motionOverlayCameraLineCheck ||
            !m_motionOverlayCenterCrosshairCheck ||
            !m_motionOverlayVelocityScaleSpin)
        {
            return;
        }

        ViewportHostWidget::MotionDebugOverlayOptions options;
        options.enabled = m_motionOverlayEnabledCheck->isChecked();
        options.showTargetMarkers = m_motionOverlayTargetMarkersCheck->isChecked();
        options.showVelocityVector = m_motionOverlayVelocityCheck->isChecked();
        options.showCameraToTargetLine = m_motionOverlayCameraLineCheck->isChecked();
        options.showScreenCenterCrosshair = m_motionOverlayCenterCrosshairCheck->isChecked();
        options.velocityScale = static_cast<float>(m_motionOverlayVelocityScaleSpin->value());
        m_viewportHost->setMotionDebugOverlayOptions(options);
    };
    connect(m_motionOverlayEnabledCheck, &QCheckBox::toggled, this, [applyMotionOverlayInspector](bool) { applyMotionOverlayInspector(); });
    connect(m_motionOverlayTargetMarkersCheck, &QCheckBox::toggled, this, [applyMotionOverlayInspector](bool) { applyMotionOverlayInspector(); });
    connect(m_motionOverlayVelocityCheck, &QCheckBox::toggled, this, [applyMotionOverlayInspector](bool) { applyMotionOverlayInspector(); });
    connect(m_motionOverlayCameraLineCheck, &QCheckBox::toggled, this, [applyMotionOverlayInspector](bool) { applyMotionOverlayInspector(); });
    connect(m_motionOverlayCenterCrosshairCheck, &QCheckBox::toggled, this, [applyMotionOverlayInspector](bool) { applyMotionOverlayInspector(); });
    connect(m_motionOverlayVelocityScaleSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [applyMotionOverlayInspector](double) { applyMotionOverlayInspector(); });

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

    // Connect follow target combo to update camera's follow target
    connect(m_followTargetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, resolveCameraIndexFromTreeItem, findFollowCameraIndexForScene](int) {
        if (m_updatingInspector || !m_viewportHost || !m_hierarchyTree || !m_followTargetCombo) return;
        QTreeWidgetItem* current = m_hierarchyTree->currentItem();
        if (!current) return;
        
        const int nodeType = current->data(0, Qt::UserRole + 3).toInt();
        const int sceneIndex = current->data(0, Qt::UserRole).toInt();
        const bool isCameraNode = nodeType == static_cast<int>(ViewportHostWidget::HierarchyNode::Type::Camera);
        const bool isSceneItemNode = nodeType == static_cast<int>(ViewportHostWidget::HierarchyNode::Type::SceneItem);
        if (!isCameraNode && !isSceneItemNode) {
            return;
        }
        
        int cameraIndex = isCameraNode
            ? resolveCameraIndexFromTreeItem(current)
            : findFollowCameraIndexForScene(sceneIndex);
        int targetIndex = m_followTargetCombo->currentData().toInt();
        if (isSceneItemNode && targetIndex >= 0 && cameraIndex < 0)
        {
            cameraIndex = m_viewportHost->createFollowCamera(sceneIndex, 5.0f, 0.0f, 20.0f);
        }
        if (isSceneItemNode && targetIndex < 0)
        {
            if (cameraIndex >= 0)
            {
                m_viewportHost->deleteCamera(cameraIndex);
                saveProjectState();
                refreshHierarchy(m_viewportHost->sceneItems());
            }
            return;
        }
        
        // Get current camera config
        auto configs = m_viewportHost->cameraConfigs();
        if (cameraIndex < 0 || cameraIndex >= configs.size()) {
            return;
        }
        
        ViewportHostWidget::CameraConfig& config = configs[cameraIndex];
        if (targetIndex >= 0) {
            // Convert to follow camera
            config.type = ViewportHostWidget::CameraConfig::Type::Follow;
            config.followTargetIndex = targetIndex;
            config.mode = QStringLiteral("CharacterFollow");
            config.freeFly = false;
            // Set default follow parameters if not already set
            if (config.followDistance < followcam::kMinDistance) config.followDistance = 5.0f;
        } else {
            // Convert to free camera
            config.type = ViewportHostWidget::CameraConfig::Type::Free;
            config.position = m_viewportHost->cameraPosition();
            config.rotation = m_viewportHost->cameraRotation();
            config.mode = QStringLiteral("FreeFly");
            config.freeFly = true;
            config.followTargetIndex = -1;
        }
        
        m_viewportHost->updateCameraConfig(cameraIndex, config);
        saveProjectState();
        refreshHierarchy(m_viewportHost->sceneItems());
    });

    // Connect follow parameter spin boxes to update camera config
    auto applyFollowParams = [this, resolveCameraIndexFromTreeItem, findFollowCameraIndexForScene]() {
        if (m_updatingInspector || !m_viewportHost || !m_hierarchyTree) return;
        QTreeWidgetItem* current = m_hierarchyTree->currentItem();
        if (!current) return;
        
        const int nodeType = current->data(0, Qt::UserRole + 3).toInt();
        const int sceneIndex = current->data(0, Qt::UserRole).toInt();
        const bool isCameraNode = nodeType == static_cast<int>(ViewportHostWidget::HierarchyNode::Type::Camera);
        const bool isSceneItemNode = nodeType == static_cast<int>(ViewportHostWidget::HierarchyNode::Type::SceneItem);
        if (!isCameraNode && !isSceneItemNode) {
            return;
        }
        
        const int cameraIndex = isCameraNode
            ? resolveCameraIndexFromTreeItem(current)
            : findFollowCameraIndexForScene(sceneIndex);
        auto configs = m_viewportHost->cameraConfigs();
        if (cameraIndex < 0 || cameraIndex >= configs.size()) {
            return;
        }
        
        ViewportHostWidget::CameraConfig config = configs[cameraIndex];
        
        if (isSceneItemNode && !config.isFollowCamera())
        {
            return;
        }

        // Update follow parameters for follow cameras and selected scene items.
        if (config.isFollowCamera()) {
            if (m_followDistanceSpin) config.followDistance = m_followDistanceSpin->value();
            if (m_followYawSpin) config.followYaw = m_followYawSpin->value();
            if (m_followPitchSpin) config.followPitch = m_followPitchSpin->value();
            if (m_followSmoothSpin) config.followSmoothSpeed = m_followSmoothSpin->value();
        }
        
        // Update clipping planes for all cameras
        if (m_nearClipSpin) config.nearClip = m_nearClipSpin->value();
        if (m_farClipSpin) config.farClip = m_farClipSpin->value();
        if (m_invertHorizontalDragCheck) config.invertHorizontalDrag = m_invertHorizontalDragCheck->isChecked();
        
        m_viewportHost->updateCameraConfig(cameraIndex, config);
        saveProjectState();
    };
    
    // Explicit WASD routing mode selector.
    auto applyWASDRoutingMode = [this, resolveCameraIndexFromTreeItem, findFollowCameraIndexForScene]() {
        if (m_updatingInspector || !m_viewportHost || !m_hierarchyTree || !m_wasdRoutingCombo) return;
        QTreeWidgetItem* current = m_hierarchyTree->currentItem();
        if (!current) return;

        const int nodeType = current->data(0, Qt::UserRole + 3).toInt();
        const int sceneIndex = current->data(0, Qt::UserRole).toInt();
        const bool isCameraNode = nodeType == static_cast<int>(ViewportHostWidget::HierarchyNode::Type::Camera);
        const bool isSceneItemNode = nodeType == static_cast<int>(ViewportHostWidget::HierarchyNode::Type::SceneItem);
        if (!isCameraNode && !isSceneItemNode) {
            return;
        }

        const int cameraIndex = isCameraNode
            ? resolveCameraIndexFromTreeItem(current)
            : findFollowCameraIndexForScene(sceneIndex);
        auto configs = m_viewportHost->cameraConfigs();
        if (cameraIndex < 0 || cameraIndex >= configs.size()) {
            return;
        }

        ViewportHostWidget::CameraConfig config = configs[cameraIndex];
        const QString requestedMode = m_wasdRoutingCombo->currentData().toString();
        config.mode = requestedMode;
        config.freeFly = requestedMode.compare(QStringLiteral("FreeFly"), Qt::CaseInsensitive) == 0;

        m_viewportHost->updateCameraConfig(cameraIndex, config);

        if (m_freeFlyCameraCheck)
        {
            QSignalBlocker blocker(m_freeFlyCameraCheck);
            m_freeFlyCameraCheck->setChecked(config.freeFly);
        }

        saveProjectState();
    };

    // Handler for free fly checkbox (applies to any camera, not just follow cameras)
    auto applyFreeFlyMode = [this, resolveCameraIndexFromTreeItem, findFollowCameraIndexForScene]() {
        if (m_updatingInspector || !m_viewportHost || !m_hierarchyTree || !m_freeFlyCameraCheck) return;
        QTreeWidgetItem* current = m_hierarchyTree->currentItem();
        if (!current) return;
        
        const int nodeType = current->data(0, Qt::UserRole + 3).toInt();
        const int sceneIndex = current->data(0, Qt::UserRole).toInt();
        const bool isCameraNode = nodeType == static_cast<int>(ViewportHostWidget::HierarchyNode::Type::Camera);
        const bool isSceneItemNode = nodeType == static_cast<int>(ViewportHostWidget::HierarchyNode::Type::SceneItem);
        if (!isCameraNode && !isSceneItemNode) {
            return;
        }
        
        const int cameraIndex = isCameraNode
            ? resolveCameraIndexFromTreeItem(current)
            : findFollowCameraIndexForScene(sceneIndex);
        auto configs = m_viewportHost->cameraConfigs();
        if (cameraIndex < 0 || cameraIndex >= configs.size()) {
            return;
        }
        
        ViewportHostWidget::CameraConfig config = configs[cameraIndex];
        config.freeFly = m_freeFlyCameraCheck->isChecked();
        if (config.freeFly)
        {
            config.mode = QStringLiteral("FreeFly");
        }
        else if (config.isFollowCamera())
        {
            const bool wasCharacterFollow =
                config.mode.compare(QStringLiteral("CharacterFollow"), Qt::CaseInsensitive) == 0;
            // Leaving free-fly on a follow camera should restore follow routing,
            // not park the camera in Fixed mode (which disables WASD passthrough).
            config.mode = wasCharacterFollow ? QStringLiteral("CharacterFollow")
                                             : QStringLiteral("OrbitFollow");
        }
        else
        {
            config.mode = QStringLiteral("Fixed");
        }

        if (m_wasdRoutingCombo)
        {
            const int comboIndex = m_wasdRoutingCombo->findData(config.mode);
            if (comboIndex >= 0)
            {
                QSignalBlocker blocker(m_wasdRoutingCombo);
                m_wasdRoutingCombo->setCurrentIndex(comboIndex);
            }
        }
        
        m_viewportHost->updateCameraConfig(cameraIndex, config);
        saveProjectState();
    };
    
    connect(m_followDistanceSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [applyFollowParams](double) { applyFollowParams(); });
    connect(m_followYawSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [applyFollowParams](double) { applyFollowParams(); });
    connect(m_followPitchSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [applyFollowParams](double) { applyFollowParams(); });
    connect(m_followSmoothSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [applyFollowParams](double) { applyFollowParams(); });
    connect(m_nearClipSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [applyFollowParams](double) { applyFollowParams(); });
    connect(m_farClipSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [applyFollowParams](double) { applyFollowParams(); });
    connect(m_invertHorizontalDragCheck, &QCheckBox::toggled, this, [applyFollowParams](bool) { applyFollowParams(); });
    connect(m_wasdRoutingCombo, &QComboBox::currentIndexChanged, this, [applyWASDRoutingMode](int) { applyWASDRoutingMode(); });
    connect(m_freeFlyCameraCheck, &QCheckBox::toggled, this, [applyFreeFlyMode](bool) { applyFreeFlyMode(); });

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

        QObject* source = sender();
        if (m_lockScaleXYZCheck && m_lockScaleXYZCheck->isChecked())
        {
            if (source == m_inspectorScaleX || source == m_inspectorScaleY || source == m_inspectorScaleZ)
            {
                const float uniform = static_cast<float>(source == m_inspectorScaleX ? m_inspectorScaleX->value()
                                                  : source == m_inspectorScaleY ? m_inspectorScaleY->value()
                                                                                : m_inspectorScaleZ->value());
                const QSignalBlocker bx(m_inspectorScaleX);
                const QSignalBlocker by(m_inspectorScaleY);
                const QSignalBlocker bz(m_inspectorScaleZ);
                m_inspectorScaleX->setValue(uniform);
                m_inspectorScaleY->setValue(uniform);
                m_inspectorScaleZ->setValue(uniform);
                scale = QVector3D(uniform, uniform, uniform);
            }
        }
        // Handle Camera and Follow Camera entries (any row <= kHierarchyCameraIndex)
        if (row <= MainWindowShell::kHierarchyCameraIndex && m_viewportHost) {
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
            if (m_boundsSizeValue && m_viewportHost)
            {
                const QVector3D bounds = m_viewportHost->sceneItemBoundsSize(row);
                m_boundsSizeValue->setText(QStringLiteral("%1 x %2 x %3")
                    .arg(QString::number(bounds.x(), 'f', 3))
                    .arg(QString::number(bounds.y(), 'f', 3))
                    .arg(QString::number(bounds.z(), 'f', 3)));
            }
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

    auto* boundsRefreshTimer = new QTimer(this);
    boundsRefreshTimer->setInterval(150);
    connect(boundsRefreshTimer, &QTimer::timeout, this, [this]() {
        if (!m_boundsSizeValue || !m_viewportHost || !m_hierarchyTree)
        {
            return;
        }
        QTreeWidgetItem* current = m_hierarchyTree->currentItem();
        const int row = current ? current->data(0, Qt::UserRole).toInt() : -1;
        if (row < 0 || row >= m_sceneItems.size())
        {
            return;
        }
        const QVector3D bounds = m_viewportHost->sceneItemBoundsSize(row);
        m_boundsSizeValue->setText(QStringLiteral("%1 x %2 x %3")
            .arg(QString::number(bounds.x(), 'f', 3))
            .arg(QString::number(bounds.y(), 'f', 3))
            .arg(QString::number(bounds.z(), 'f', 3)));
    });
    boundsRefreshTimer->start();

    setupProjectMenu();
    
    // Initialize undo stack
    m_undoStack = new QUndoStack(this);
    setupUndoShortcuts();
    
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

bool MainWindowShell::selectHierarchySceneItem(int sceneIndex)
{
    if (!m_hierarchyTree || sceneIndex < 0)
    {
        return false;
    }

    const QList<QTreeWidgetItem*> items =
        m_hierarchyTree->findItems(QStringLiteral("*"), Qt::MatchWildcard | Qt::MatchRecursive);
    for (QTreeWidgetItem* item : items)
    {
        if (!item)
        {
            continue;
        }
        const int nodeType = item->data(0, Qt::UserRole + 3).toInt();
        if (nodeType != static_cast<int>(ViewportHostWidget::HierarchyNode::Type::SceneItem))
        {
            continue;
        }
        if (item->data(0, Qt::UserRole).toInt() != sceneIndex)
        {
            continue;
        }
        m_hierarchyTree->setCurrentItem(item);
        item->setSelected(true);
        updateInspectorForSelection(item);
        return true;
    }
    return false;
}

bool MainWindowShell::selectHierarchyCamera(const QString& cameraId, int cameraIndex)
{
    if (!m_hierarchyTree)
    {
        return false;
    }

    const QList<QTreeWidgetItem*> items =
        m_hierarchyTree->findItems(QStringLiteral("*"), Qt::MatchWildcard | Qt::MatchRecursive);
    for (QTreeWidgetItem* item : items)
    {
        if (!item)
        {
            continue;
        }
        const int nodeType = item->data(0, Qt::UserRole + 3).toInt();
        if (nodeType != static_cast<int>(ViewportHostWidget::HierarchyNode::Type::Camera))
        {
            continue;
        }

        if (!cameraId.isEmpty() && item->data(0, Qt::UserRole + 6).toString() == cameraId)
        {
            m_hierarchyTree->setCurrentItem(item);
            item->setSelected(true);
            updateInspectorForSelection(item);
            return true;
        }

        if (cameraId.isEmpty() && cameraIndex >= 0 && item->data(0, Qt::UserRole + 5).toInt() == cameraIndex)
        {
            m_hierarchyTree->setCurrentItem(item);
            item->setSelected(true);
            updateInspectorForSelection(item);
            return true;
        }
    }
    return false;
}

QJsonObject MainWindowShell::inspectorDebugJson() const
{
    QJsonObject payload;
    payload.insert(QStringLiteral("ok"), true);

    QTreeWidgetItem* current = m_hierarchyTree ? m_hierarchyTree->currentItem() : nullptr;
    payload.insert(QStringLiteral("hasSelection"), current != nullptr);
    payload.insert(QStringLiteral("selectedLabel"), current ? current->text(0) : QString());
    payload.insert(QStringLiteral("selectedSceneIndex"), current ? current->data(0, Qt::UserRole).toInt() : -1);
    payload.insert(QStringLiteral("selectedMeshIndex"), current ? current->data(0, Qt::UserRole + 1).toInt() : -1);
    payload.insert(QStringLiteral("selectedPrimitiveIndex"), current ? current->data(0, Qt::UserRole + 2).toInt() : -1);
    payload.insert(QStringLiteral("selectedNodeType"), current ? current->data(0, Qt::UserRole + 3).toInt() : -1);
    payload.insert(QStringLiteral("selectedCameraIndex"), current ? current->data(0, Qt::UserRole + 5).toInt() : -1);
    payload.insert(QStringLiteral("selectedCameraId"), current ? current->data(0, Qt::UserRole + 6).toString() : QString());

    payload.insert(QStringLiteral("followCamInfoText"), m_objectFollowCamInfoValue ? m_objectFollowCamInfoValue->text() : QString());
    payload.insert(QStringLiteral("kinematicInfoText"), m_objectKinematicInfoValue ? m_objectKinematicInfoValue->text() : QString());
    payload.insert(QStringLiteral("animationRuntimeInfoText"), m_objectAnimationRuntimeInfoValue ? m_objectAnimationRuntimeInfoValue->text() : QString());

    payload.insert(QStringLiteral("followDistanceVisible"), m_followDistanceSpin ? !m_followDistanceSpin->isHidden() : false);
    payload.insert(QStringLiteral("followYawVisible"), m_followYawSpin ? !m_followYawSpin->isHidden() : false);
    payload.insert(QStringLiteral("followPitchVisible"), m_followPitchSpin ? !m_followPitchSpin->isHidden() : false);
    payload.insert(QStringLiteral("followSmoothVisible"), m_followSmoothSpin ? !m_followSmoothSpin->isHidden() : false);
    payload.insert(QStringLiteral("followDistanceEnabled"), m_followDistanceSpin ? m_followDistanceSpin->isEnabled() : false);
    payload.insert(QStringLiteral("followYawEnabled"), m_followYawSpin ? m_followYawSpin->isEnabled() : false);
    payload.insert(QStringLiteral("followPitchEnabled"), m_followPitchSpin ? m_followPitchSpin->isEnabled() : false);
    payload.insert(QStringLiteral("followSmoothEnabled"), m_followSmoothSpin ? m_followSmoothSpin->isEnabled() : false);
    payload.insert(QStringLiteral("followDistance"), m_followDistanceSpin ? m_followDistanceSpin->value() : 0.0);
    payload.insert(QStringLiteral("followYaw"), m_followYawSpin ? m_followYawSpin->value() : 0.0);
    payload.insert(QStringLiteral("followPitch"), m_followPitchSpin ? m_followPitchSpin->value() : 0.0);
    payload.insert(QStringLiteral("followSmooth"), m_followSmoothSpin ? m_followSmoothSpin->value() : 0.0);

    QJsonArray hierarchyState;
    if (m_hierarchyTree)
    {
        const QList<QTreeWidgetItem*> allItems =
            m_hierarchyTree->findItems(QStringLiteral("*"), Qt::MatchWildcard | Qt::MatchRecursive);
        int expandedCount = 0;
        for (QTreeWidgetItem* item : allItems)
        {
            if (!item)
            {
                continue;
            }
            if (item->isExpanded())
            {
                ++expandedCount;
            }
            QJsonObject node{
                {QStringLiteral("label"), item->text(0)},
                {QStringLiteral("expanded"), item->isExpanded()},
                {QStringLiteral("childCount"), item->childCount()},
                {QStringLiteral("sceneIndex"), item->data(0, Qt::UserRole).toInt()},
                {QStringLiteral("nodeType"), item->data(0, Qt::UserRole + 3).toInt()}
            };
            hierarchyState.append(node);
        }
        payload.insert(QStringLiteral("hierarchyItemCount"), allItems.size());
        payload.insert(QStringLiteral("hierarchyExpandedCount"), expandedCount);
    }
    payload.insert(QStringLiteral("hierarchy"), hierarchyState);

    return payload;
}

QWidget* MainWindowShell::wrapTabInScrollArea(QWidget* content) const
{
    auto* scroll = new QScrollArea(m_rightTabs);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setWidget(content);
    return scroll;
}

QJsonObject MainWindowShell::uiDebugJson() const
{
    QJsonObject payload;
    payload.insert(QStringLiteral("window"), widgetMetrics(this));
    payload.insert(QStringLiteral("centralWidget"), widgetMetrics(centralWidget()));
    payload.insert(QStringLiteral("mainSplitter"), splitterMetrics(m_splitter));
    payload.insert(QStringLiteral("leftPane"), widgetMetrics(m_leftPane));
    payload.insert(QStringLiteral("viewportHost"), widgetMetrics(m_viewportHost));

    QJsonArray splitterArray;
    for (QSplitter* splitter : findChildren<QSplitter*>())
    {
        splitterArray.append(splitterMetrics(splitter));
    }
    payload.insert(QStringLiteral("splitters"), splitterArray);

    QJsonArray dockArray;
    const QList<QDockWidget*> docks = findChildren<QDockWidget*>();
    for (QDockWidget* dock : docks)
    {
        QJsonObject dockObject = widgetMetrics(dock);
        dockObject.insert(QStringLiteral("features"), static_cast<int>(dock->features()));
        dockObject.insert(QStringLiteral("allowedAreas"), static_cast<int>(dock->allowedAreas()));
        dockObject.insert(QStringLiteral("area"), static_cast<int>(dockWidgetArea(dock)));
        dockArray.append(dockObject);
    }
    payload.insert(QStringLiteral("dockWidgets"), dockArray);

    QJsonObject rightTabsObject;
    rightTabsObject.insert(QStringLiteral("metrics"), widgetMetrics(m_rightTabs));
    if (m_rightTabs)
    {
        rightTabsObject.insert(QStringLiteral("currentIndex"), m_rightTabs->currentIndex());
        rightTabsObject.insert(QStringLiteral("currentTabLabel"), m_rightTabs->tabText(m_rightTabs->currentIndex()));
        rightTabsObject.insert(QStringLiteral("tabCount"), m_rightTabs->count());
        QJsonArray tabNames;
        for (int i = 0; i < m_rightTabs->count(); ++i)
        {
            tabNames.append(m_rightTabs->tabText(i));
        }
        rightTabsObject.insert(QStringLiteral("tabNames"), tabNames);
    }
    payload.insert(QStringLiteral("rightTabs"), rightTabsObject);

    QJsonObject elementTabsObject;
    elementTabsObject.insert(QStringLiteral("metrics"), widgetMetrics(m_elementDetailTabs));
    if (m_elementDetailTabs)
    {
        elementTabsObject.insert(QStringLiteral("currentIndex"), m_elementDetailTabs->currentIndex());
        elementTabsObject.insert(QStringLiteral("currentTabLabel"), m_elementDetailTabs->tabText(m_elementDetailTabs->currentIndex()));
        elementTabsObject.insert(QStringLiteral("tabCount"), m_elementDetailTabs->count());
        QJsonArray tabNames;
        for (int i = 0; i < m_elementDetailTabs->count(); ++i)
        {
            tabNames.append(m_elementDetailTabs->tabText(i));
        }
        elementTabsObject.insert(QStringLiteral("tabNames"), tabNames);
    }
    payload.insert(QStringLiteral("elementDetailTabs"), elementTabsObject);

    QJsonObject inspectorWidgets;
    inspectorWidgets.insert(QStringLiteral("cameraSection"), widgetMetrics(m_cameraSection));
    inspectorWidgets.insert(QStringLiteral("cameraTypeValue"), widgetMetrics(m_cameraTypeValue));
    inspectorWidgets.insert(QStringLiteral("transformSection"), widgetMetrics(m_transformSection));
    inspectorWidgets.insert(QStringLiteral("materialSection"), widgetMetrics(m_materialSection));
    inspectorWidgets.insert(QStringLiteral("animationSection"), widgetMetrics(m_animationSection));
    inspectorWidgets.insert(QStringLiteral("physicsSection"), widgetMetrics(m_physicsSection));
    inspectorWidgets.insert(QStringLiteral("lightSection"), widgetMetrics(m_lightSection));
    inspectorWidgets.insert(QStringLiteral("runtimeSection"), widgetMetrics(m_runtimeSection));
    inspectorWidgets.insert(QStringLiteral("followTargetCombo"), widgetMetrics(m_followTargetCombo));
    inspectorWidgets.insert(QStringLiteral("followDistanceSpin"), widgetMetrics(m_followDistanceSpin));
    inspectorWidgets.insert(QStringLiteral("followYawSpin"), widgetMetrics(m_followYawSpin));
    inspectorWidgets.insert(QStringLiteral("followPitchSpin"), widgetMetrics(m_followPitchSpin));
    inspectorWidgets.insert(QStringLiteral("followSmoothSpin"), widgetMetrics(m_followSmoothSpin));
    inspectorWidgets.insert(QStringLiteral("wasdRoutingCombo"), widgetMetrics(m_wasdRoutingCombo));
    inspectorWidgets.insert(QStringLiteral("freeFlyCameraCheck"), widgetMetrics(m_freeFlyCameraCheck));
    inspectorWidgets.insert(QStringLiteral("nearClipSpin"), widgetMetrics(m_nearClipSpin));
    inspectorWidgets.insert(QStringLiteral("farClipSpin"), widgetMetrics(m_farClipSpin));

    QJsonObject inspector = inspectorDebugJson();
    inspector.insert(QStringLiteral("widgets"), inspectorWidgets);
    payload.insert(QStringLiteral("inspector"), inspector);
    return payload;
}

bool MainWindowShell::eventFilter(QObject* watched, QEvent* event)
{
    if (m_hierarchyTree && watched == m_hierarchyTree->viewport() && event)
    {
        if (event->type() == QEvent::MouseButtonPress)
        {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent && mouseEvent->button() == Qt::RightButton)
            {
                m_suppressHierarchySelectionEffects = true;
            }
        }
        else if (event->type() == QEvent::MouseButtonRelease)
        {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent && mouseEvent->button() == Qt::RightButton)
            {
                QTimer::singleShot(0, this, [this]()
                {
                    m_suppressHierarchySelectionEffects = false;
                });
            }
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindowShell::closeEvent(QCloseEvent* event)
{
    saveUiState();
    QMainWindow::closeEvent(event);
}

void MainWindowShell::setupUndoShortcuts()
{
    if (!m_undoStack)
        return;
    
    // Ctrl+Z for undo
    auto* undoShortcut = new QShortcut(QKeySequence::Undo, this);
    connect(undoShortcut, &QShortcut::activated, m_undoStack, &QUndoStack::undo);
    
    // Ctrl+Y or Ctrl+Shift+Z for redo
    auto* redoShortcut = new QShortcut(QKeySequence::Redo, this);
    connect(redoShortcut, &QShortcut::activated, m_undoStack, &QUndoStack::redo);
    
    // Also add menu actions for discoverability
    if (QMenu* editMenu = menuBar()->addMenu(QStringLiteral("Edit")))
    {
        QAction* undoAction = m_undoStack->createUndoAction(this, QStringLiteral("Undo"));
        undoAction->setShortcuts(QKeySequence::Undo);
        editMenu->addAction(undoAction);
        
        QAction* redoAction = m_undoStack->createRedoAction(this, QStringLiteral("Redo"));
        redoAction->setShortcuts(QKeySequence::Redo);
        editMenu->addAction(redoAction);
    }
}

void MainWindowShell::pushTransformCommand(
    int sceneIndex,
    const QVector3D& oldTranslation,
    const QVector3D& oldRotation,
    const QVector3D& oldScale,
    const QVector3D& newTranslation,
    const QVector3D& newRotation,
    const QVector3D& newScale)
{
    if (!m_undoStack || sceneIndex < 0 || sceneIndex >= m_sceneItems.size())
        return;
    
    QString itemName = m_sceneItems[sceneIndex].name;
    
    // Create apply callback that updates both the viewport and our local state
    auto applyCallback = [this](int index, const QVector3D& trans, const QVector3D& rot, const QVector3D& scale)
    {
        if (index < 0 || index >= m_sceneItems.size())
            return;
        
        // Update local state
        m_sceneItems[index].translation = trans;
        m_sceneItems[index].rotation = rot;
        m_sceneItems[index].scale = scale;
        
        // Update viewport
        if (m_viewportHost)
        {
            m_viewportHost->updateSceneItemTransform(index, trans, rot, scale);
        }
        
        // Update inspector if this is the currently selected item
        // (without triggering another undo command)
        if (!m_updatingInspector)
        {
            QTreeWidgetItem* currentItem = m_hierarchyTree->currentItem();
            if (currentItem)
            {
                int currentIndex = currentItem->data(0, Qt::UserRole).toInt();
                if (currentIndex == index)
                {
                    m_updatingInspector = true;
                    m_inspectorTranslationX->setValue(trans.x());
                    m_inspectorTranslationY->setValue(trans.y());
                    m_inspectorTranslationZ->setValue(trans.z());
                    m_inspectorRotationX->setValue(rot.x());
                    m_inspectorRotationY->setValue(rot.y());
                    m_inspectorRotationZ->setValue(rot.z());
                    m_inspectorScaleX->setValue(scale.x());
                    m_inspectorScaleY->setValue(scale.y());
                    m_inspectorScaleZ->setValue(scale.z());
                    m_updatingInspector = false;
                }
            }
        }
        
        saveProjectState();
    };
    
    auto* cmd = new TransformUndoCommand(
        sceneIndex,
        oldTranslation, oldRotation, oldScale,
        newTranslation, newRotation, newScale,
        itemName,
        applyCallback
    );
    
    m_undoStack->push(cmd);
}

}  // namespace motive::ui
