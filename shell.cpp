#include "shell.h"
#include "asset_browser_widget.h"
#include "host_widget.h"
#include "transform_undo_command.h"
#include "physics_interface.h"

#include <glm/glm.hpp>

#include <QShortcut>
#include <QUndoView>

#include <QAbstractItemView>
#include <QAction>
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
#include <QPixmap>
#include <QPushButton>
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
            distSpin->setRange(0.5, 100.0);
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
    auto* lightColorContainer = new QWidget(inspectorPanel);
    auto* lightColorLayout = new QHBoxLayout(lightColorContainer);
    lightColorLayout->setContentsMargins(0, 0, 0, 0);
    lightColorLayout->addWidget(m_lightColorWidget);
    lightColorLayout->addWidget(lightColorButton);
    lightColorLayout->addStretch(1);

    // Create follow target selector for cameras
    m_followTargetCombo = new QComboBox(inspectorPanel);
    m_followTargetCombo->addItem(QStringLiteral("None (Free Camera)"), -1);
    m_followTargetLabel = new QLabel(inspectorPanel);
    
    // Free fly camera toggle
    m_freeFlyCameraCheck = new QCheckBox(QStringLiteral("Free fly mode (WASD moves camera)"), inspectorPanel);
    m_freeFlyCameraCheck->setChecked(true);
    m_freeFlyCameraCheck->setToolTip(QStringLiteral("When enabled, WASD moves the camera. When disabled, WASD controls the character and right-drag orbits."));
    
    // Near/Far clipping planes
    m_nearClipSpin = createSpinBox(inspectorPanel, 0.001, 100.0, 0.01);
    m_nearClipSpin->setValue(0.1);
    m_nearClipSpin->setToolTip(QStringLiteral("Near clipping plane distance"));
    m_farClipSpin = createSpinBox(inspectorPanel, 0.1, 10000.0, 0.1);
    m_farClipSpin->setValue(100.0);
    m_farClipSpin->setToolTip(QStringLiteral("Far clipping plane distance"));
    
    // Create follow camera parameter controls
    m_followParamsLabel = new QLabel(QStringLiteral("Follow Parameters"), inspectorPanel);
    m_followDistanceSpin = createSpinBox(inspectorPanel, 0.1, 100.0, 0.1);
    m_followDistanceSpin->setValue(5.0);
    m_followYawSpin = createSpinBox(inspectorPanel, -360.0, 360.0, 1.0);
    m_followYawSpin->setValue(0.0);
    m_followPitchSpin = createSpinBox(inspectorPanel, -90.0, 90.0, 1.0);
    m_followPitchSpin->setValue(20.0);
    m_followSmoothSpin = createSpinBox(inspectorPanel, 0.1, 50.0, 0.1);
    m_followSmoothSpin->setValue(5.0);

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
    inspectorLayout->addRow(QStringLiteral("Camera Mode"), m_freeFlyCameraCheck);
    inspectorLayout->addRow(QStringLiteral("Near Clip"), m_nearClipSpin);
    inspectorLayout->addRow(QStringLiteral("Far Clip"), m_farClipSpin);
    inspectorLayout->addRow(QStringLiteral("Follow Target"), m_followTargetCombo);
    inspectorLayout->addRow(m_followParamsLabel);
    inspectorLayout->addRow(QStringLiteral("Distance"), m_followDistanceSpin);
    inspectorLayout->addRow(QStringLiteral("Yaw"), m_followYawSpin);
    inspectorLayout->addRow(QStringLiteral("Pitch"), m_followPitchSpin);
    inspectorLayout->addRow(QStringLiteral("Smooth Speed"), m_followSmoothSpin);
    inspectorLayout->addRow(QStringLiteral("Light Type"), m_lightTypeCombo);
    inspectorLayout->addRow(QStringLiteral("Brightness"), m_lightBrightnessSpin);
    inspectorLayout->addRow(QStringLiteral("Color"), lightColorContainer);
    inspectorLayout->addRow(QStringLiteral("Translation"), translationWidget);
    inspectorLayout->addRow(QStringLiteral("Rotation"), rotationWidget);
    inspectorLayout->addRow(QStringLiteral("Scale"), scaleWidget);
    inspectorLayout->addRow(QStringLiteral("Physics Gravity"), m_elementUseGravityCheck);
    inspectorLayout->addRow(QStringLiteral("Custom Gravity"), m_elementGravityWidget);
    m_rightTabs->addTab(inspectorPanel, QStringLiteral("Element"));
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

    connect(m_hierarchyTree, &QTreeWidget::currentItemChanged, this, [this, resolveCameraIndexFromTreeItem](QTreeWidgetItem* current)
    {
        // Check if a camera node was selected and switch to it
        if (current && m_viewportHost)
        {
            const int type = current->data(0, Qt::UserRole + 3).toInt();
            if (type == static_cast<int>(ViewportHostWidget::HierarchyNode::Type::Camera))
            {
                const int cameraIndex = resolveCameraIndexFromTreeItem(current);
                if (cameraIndex >= 0)
                {
                    m_viewportHost->setActiveCamera(cameraIndex);
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
    connect(m_followTargetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, resolveCameraIndexFromTreeItem](int) {
        if (m_updatingInspector || !m_viewportHost || !m_hierarchyTree || !m_followTargetCombo) return;
        QTreeWidgetItem* current = m_hierarchyTree->currentItem();
        if (!current) return;
        
        // Check if a camera is selected
        const int nodeType = current->data(0, Qt::UserRole + 3).toInt();
        if (nodeType != static_cast<int>(ViewportHostWidget::HierarchyNode::Type::Camera)) {
            return;
        }
        
        const int cameraIndex = resolveCameraIndexFromTreeItem(current);
        const int targetIndex = m_followTargetCombo->currentData().toInt();
        
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
            // Set default follow parameters if not already set
            if (config.followDistance <= 0) config.followDistance = 5.0f;
        } else {
            // Convert to free camera
            config.type = ViewportHostWidget::CameraConfig::Type::Free;
            config.position = m_viewportHost->cameraPosition();
            config.rotation = m_viewportHost->cameraRotation();
        }
        
        m_viewportHost->updateCameraConfig(cameraIndex, config);
        saveProjectState();
        refreshHierarchy(m_viewportHost->sceneItems());
    });

    // Connect follow parameter spin boxes to update camera config
    auto applyFollowParams = [this, resolveCameraIndexFromTreeItem]() {
        if (m_updatingInspector || !m_viewportHost || !m_hierarchyTree) return;
        QTreeWidgetItem* current = m_hierarchyTree->currentItem();
        if (!current) return;
        
        const int nodeType = current->data(0, Qt::UserRole + 3).toInt();
        if (nodeType != static_cast<int>(ViewportHostWidget::HierarchyNode::Type::Camera)) {
            return;
        }
        
        const int cameraIndex = resolveCameraIndexFromTreeItem(current);
        auto configs = m_viewportHost->cameraConfigs();
        if (cameraIndex < 0 || cameraIndex >= configs.size()) {
            return;
        }
        
        ViewportHostWidget::CameraConfig config = configs[cameraIndex];
        
        // Update follow parameters from spin boxes (only for follow cameras)
        if (config.isFollowCamera()) {
            if (m_followDistanceSpin) config.followDistance = m_followDistanceSpin->value();
            if (m_followYawSpin) config.followYaw = m_followYawSpin->value();
            if (m_followPitchSpin) config.followPitch = m_followPitchSpin->value();
            if (m_followSmoothSpin) config.followSmoothSpeed = m_followSmoothSpin->value();
        }
        
        // Update clipping planes for all cameras
        if (m_nearClipSpin) config.nearClip = m_nearClipSpin->value();
        if (m_farClipSpin) config.farClip = m_farClipSpin->value();
        
        m_viewportHost->updateCameraConfig(cameraIndex, config);
        saveProjectState();
    };
    
    // Handler for free fly checkbox (applies to any camera, not just follow cameras)
    auto applyFreeFlyMode = [this, resolveCameraIndexFromTreeItem]() {
        if (m_updatingInspector || !m_viewportHost || !m_hierarchyTree || !m_freeFlyCameraCheck) return;
        QTreeWidgetItem* current = m_hierarchyTree->currentItem();
        if (!current) return;
        
        const int nodeType = current->data(0, Qt::UserRole + 3).toInt();
        if (nodeType != static_cast<int>(ViewportHostWidget::HierarchyNode::Type::Camera)) {
            return;
        }
        
        const int cameraIndex = resolveCameraIndexFromTreeItem(current);
        auto configs = m_viewportHost->cameraConfigs();
        if (cameraIndex < 0 || cameraIndex >= configs.size()) {
            return;
        }
        
        ViewportHostWidget::CameraConfig config = configs[cameraIndex];
        config.freeFly = m_freeFlyCameraCheck->isChecked();
        
        m_viewportHost->updateCameraConfig(cameraIndex, config);
        saveProjectState();
    };
    
    connect(m_followDistanceSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [applyFollowParams](double) { applyFollowParams(); });
    connect(m_followYawSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [applyFollowParams](double) { applyFollowParams(); });
    connect(m_followPitchSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [applyFollowParams](double) { applyFollowParams(); });
    connect(m_followSmoothSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [applyFollowParams](double) { applyFollowParams(); });
    connect(m_nearClipSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [applyFollowParams](double) { applyFollowParams(); });
    connect(m_farClipSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [applyFollowParams](double) { applyFollowParams(); });
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
