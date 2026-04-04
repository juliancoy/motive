#include "main_window_shell.h"
#include "asset_browser_widget.h"
#include "viewport_host_widget.h"

#include <QAbstractItemView>
#include <QAction>
#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

namespace motive::ui {

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
        
        // Add Follow submenu
        QMenu* followMenu = menu.addMenu(QStringLiteral("Follow"));
        bool hasFollowCam = m_viewportHost && m_viewportHost->hasFollowCamera(row);
        QAction* createFollowAction = followMenu->addAction(hasFollowCam ? QStringLiteral("Jump to Follow Cam") : QStringLiteral("Create Follow Cam"));
        QAction* configureFollowAction = followMenu->addAction(QStringLiteral("Configure..."));
        QAction* exitFollowAction = followMenu->addAction(QStringLiteral("Exit Follow Mode"));
        exitFollowAction->setEnabled(hasFollowCam);
        
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
            // Get current settings or defaults
            ViewportHostWidget::FollowCameraSettings settings = m_viewportHost->currentFollowSettings();
            
            // Create or jump to follow camera
            if (m_viewportHost->createOrJumpToFollowCamera(row, settings))
            {
                // Update hierarchy to show camera change (optional UI feedback)
                updateInspectorForSelection(m_hierarchyTree->currentItem());
            }
        }
        else if (chosen == configureFollowAction)
        {
            // Show dialog to configure follow settings
            ViewportHostWidget::FollowCameraSettings settings = m_viewportHost->currentFollowSettings();
            
            QDialog dialog(this);
            dialog.setWindowTitle(QStringLiteral("Follow Camera Settings"));
            auto* layout = new QVBoxLayout(&dialog);
            auto* formLayout = new QFormLayout();
            
            auto* yawSpin = new QDoubleSpinBox(&dialog);
            yawSpin->setRange(-180.0, 180.0);
            yawSpin->setValue(settings.relativeYaw);
            yawSpin->setSuffix(QStringLiteral("°"));
            yawSpin->setDecimals(1);
            formLayout->addRow(QStringLiteral("Horizontal Angle:"), yawSpin);
            
            auto* pitchSpin = new QDoubleSpinBox(&dialog);
            pitchSpin->setRange(-89.0, 89.0);
            pitchSpin->setValue(settings.relativePitch);
            pitchSpin->setSuffix(QStringLiteral("°"));
            pitchSpin->setDecimals(1);
            formLayout->addRow(QStringLiteral("Vertical Angle:"), pitchSpin);
            
            auto* distSpin = new QDoubleSpinBox(&dialog);
            distSpin->setRange(0.5, 100.0);
            distSpin->setValue(settings.distance);
            distSpin->setSingleStep(0.5);
            distSpin->setDecimals(2);
            formLayout->addRow(QStringLiteral("Distance:"), distSpin);
            
            auto* smoothSpin = new QDoubleSpinBox(&dialog);
            smoothSpin->setRange(0.1, 50.0);
            smoothSpin->setValue(settings.smoothSpeed);
            smoothSpin->setSingleStep(0.5);
            smoothSpin->setDecimals(1);
            smoothSpin->setToolTip(QStringLiteral("Higher = snappier, Lower = smoother"));
            formLayout->addRow(QStringLiteral("Smoothing:"), smoothSpin);
            
            layout->addLayout(formLayout);
            
            auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
            layout->addWidget(buttonBox);
            
            connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            
            if (dialog.exec() == QDialog::Accepted)
            {
                settings.relativeYaw = static_cast<float>(yawSpin->value());
                settings.relativePitch = static_cast<float>(pitchSpin->value());
                settings.distance = static_cast<float>(distSpin->value());
                settings.smoothSpeed = static_cast<float>(smoothSpin->value());
                
                m_viewportHost->setFollowCameraSettings(settings);
                
                // If already following, update immediately
                if (m_viewportHost->hasFollowCamera(row))
                {
                    m_viewportHost->createOrJumpToFollowCamera(row, settings);
                }
            }
        }
        else if (chosen == exitFollowAction)
        {
            m_viewportHost->exitFollowCamera();
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
            m_hierarchyTree->currentItem()->data(0, Qt::UserRole).toInt() == MainWindowShell::kHierarchyCameraIndex)
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
        if (row == MainWindowShell::kHierarchyCameraIndex && m_viewportHost) {
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

}  // namespace motive::ui
