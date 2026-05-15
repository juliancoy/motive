#include "shell.h"
#include "host_widget.h"
#include "physics_interface.h"

#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>

namespace motive::ui {

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

    m_viewportCountCombo = new QComboBox(cameraPanel);
    m_viewportCountCombo->addItem(QStringLiteral("1"), 1);
    m_viewportCountCombo->addItem(QStringLiteral("2"), 2);
    m_viewportCountCombo->addItem(QStringLiteral("3"), 3);
    m_viewportCountCombo->addItem(QStringLiteral("4"), 4);
    m_viewportCountCombo->setToolTip(QStringLiteral("Choose how many viewport slots are shown."));
    cameraLayout->addRow(QStringLiteral("Viewport Slots"), m_viewportCountCombo);
    connect(m_viewportCountCombo, &QComboBox::currentIndexChanged, this, [this]() {
        if (m_updatingCameraSettings || !m_viewportHost || !m_viewportCountCombo) return;
        m_viewportHost->setViewportCount(m_viewportCountCombo->currentData().toInt());
        saveProjectState();
    });

    auto* physicsSection = new QGroupBox(QStringLiteral("Physics"), cameraPanel);
    auto* physicsLayout = new QFormLayout(physicsSection);

    m_globalPhysicsEngineCombo = new QComboBox(physicsSection);
    const auto availableBackends = motive::PhysicsFactory::getAvailableBackends();
    for (const motive::PhysicsEngineType backend : availableBackends)
    {
        m_globalPhysicsEngineCombo->addItem(
            QString::fromUtf8(motive::PhysicsFactory::getBackendName(backend)),
            static_cast<int>(backend));
    }
    physicsLayout->addRow(QStringLiteral("Backend"), m_globalPhysicsEngineCombo);
    connect(m_globalPhysicsEngineCombo, &QComboBox::currentIndexChanged, this, [this]() {
        if (m_updatingCameraSettings || !m_viewportHost || !m_globalPhysicsEngineCombo) return;
        motive::PhysicsSettings settings = m_viewportHost->globalPhysicsSettings();
        settings.engineType = static_cast<motive::PhysicsEngineType>(m_globalPhysicsEngineCombo->currentData().toInt());
        m_viewportHost->setGlobalPhysicsSettings(settings);
        saveProjectState();
    });

    auto* gravityWidget = new QWidget(physicsSection);
    auto* gravityLayout = new QHBoxLayout(gravityWidget);
    gravityLayout->setContentsMargins(0, 0, 0, 0);
    m_globalGravityXSpin = createSpinBox(gravityWidget, -1000.0, 1000.0, 0.1);
    m_globalGravityYSpin = createSpinBox(gravityWidget, -1000.0, 1000.0, 0.1);
    m_globalGravityZSpin = createSpinBox(gravityWidget, -1000.0, 1000.0, 0.1);
    gravityLayout->addWidget(new QLabel(QStringLiteral("X"), gravityWidget));
    gravityLayout->addWidget(m_globalGravityXSpin);
    gravityLayout->addWidget(new QLabel(QStringLiteral("Y"), gravityWidget));
    gravityLayout->addWidget(m_globalGravityYSpin);
    gravityLayout->addWidget(new QLabel(QStringLiteral("Z"), gravityWidget));
    gravityLayout->addWidget(m_globalGravityZSpin);
    physicsLayout->addRow(QStringLiteral("World Gravity"), gravityWidget);
    auto applyGlobalGravity = [this]() {
        if (m_updatingCameraSettings || !m_viewportHost ||
            !m_globalGravityXSpin || !m_globalGravityYSpin || !m_globalGravityZSpin)
        {
            return;
        }
        motive::PhysicsSettings settings = m_viewportHost->globalPhysicsSettings();
        settings.gravity = glm::vec3(
            static_cast<float>(m_globalGravityXSpin->value()),
            static_cast<float>(m_globalGravityYSpin->value()),
            static_cast<float>(m_globalGravityZSpin->value()));
        m_viewportHost->setGlobalPhysicsSettings(settings);
        saveProjectState();
    };
    connect(m_globalGravityXSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [applyGlobalGravity]() { applyGlobalGravity(); });
    connect(m_globalGravityYSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [applyGlobalGravity]() { applyGlobalGravity(); });
    connect(m_globalGravityZSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [applyGlobalGravity]() { applyGlobalGravity(); });

    m_globalPhysicsMaxSubStepsSpin = new QSpinBox(physicsSection);
    m_globalPhysicsMaxSubStepsSpin->setRange(1, 16);
    physicsLayout->addRow(QStringLiteral("Max Substeps"), m_globalPhysicsMaxSubStepsSpin);
    connect(m_globalPhysicsMaxSubStepsSpin, &QSpinBox::valueChanged, this, [this](int value) {
        if (m_updatingCameraSettings || !m_viewportHost) return;
        motive::PhysicsSettings settings = m_viewportHost->globalPhysicsSettings();
        settings.maxSubSteps = value;
        m_viewportHost->setGlobalPhysicsSettings(settings);
        saveProjectState();
    });

    m_globalPhysicsAutoSyncCheck = new QCheckBox(QStringLiteral("Auto-sync physics transforms"), physicsSection);
    physicsLayout->addRow(QStringLiteral("Sync"), m_globalPhysicsAutoSyncCheck);
    connect(m_globalPhysicsAutoSyncCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (m_updatingCameraSettings || !m_viewportHost) return;
        motive::PhysicsSettings settings = m_viewportHost->globalPhysicsSettings();
        settings.autoSync = checked;
        m_viewportHost->setGlobalPhysicsSettings(settings);
        saveProjectState();
    });

    m_globalPhysicsDebugDrawCheck = new QCheckBox(QStringLiteral("Enable physics debug draw"), physicsSection);
    physicsLayout->addRow(QStringLiteral("Debug Draw"), m_globalPhysicsDebugDrawCheck);
    connect(m_globalPhysicsDebugDrawCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (m_updatingCameraSettings || !m_viewportHost) return;
        motive::PhysicsSettings settings = m_viewportHost->globalPhysicsSettings();
        settings.debugDraw = checked;
        m_viewportHost->setGlobalPhysicsSettings(settings);
        saveProjectState();
    });

    cameraLayout->addRow(physicsSection);

    m_meshConsolidationCheck = new QCheckBox(QStringLiteral("Enable mesh consolidation"), cameraPanel);
    m_meshConsolidationCheck->setChecked(true);
    cameraLayout->addRow(QStringLiteral("Import"), m_meshConsolidationCheck);
    connect(m_meshConsolidationCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (m_updatingCameraSettings || !m_viewportHost) return;
        m_viewportHost->setMeshConsolidationEnabled(checked);
        saveProjectState();
    });
    
    // Parallel loading toggle
    auto* parallelLoadCheck = new QCheckBox(QStringLiteral("Enable parallel model loading"), cameraPanel);
    parallelLoadCheck->setChecked(true);
    parallelLoadCheck->setToolTip(QStringLiteral("Load multiple models in parallel using multiple CPU cores (requires restart)"));
    cameraLayout->addRow(QStringLiteral("Performance"), parallelLoadCheck);
    
    // Validation layers toggle
    m_validationLayersCheck = new QCheckBox(QStringLiteral("Enable Vulkan validation layers"), cameraPanel);
    m_validationLayersCheck->setChecked(true);
    m_validationRestartLabel = new QLabel(QStringLiteral("(restart required)"), cameraPanel);
    m_validationRestartLabel->setStyleSheet(QStringLiteral("color: #a0a0a0; font-size: 11px;"));
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

    auto* centerAllButton = new QPushButton(QStringLiteral("Center All"), cameraPanel);
    centerAllButton->setToolTip(QStringLiteral("Recenter all scene items so the hierarchy centroid is at world origin."));
    cameraLayout->addRow(QStringLiteral("Scene"), centerAllButton);
    connect(centerAllButton, &QPushButton::clicked, this, [this]() {
        centerAllSceneItemsToOrigin();
    });

    m_saveProjectButton = new QPushButton(QStringLiteral("Save Project JSON"), cameraPanel);
    m_saveProjectButton->setToolTip(
        QStringLiteral("Write all current object metadata, lights, cameras, physics, and UI state to the existing project JSON file."));
    cameraLayout->addRow(QStringLiteral("Project"), m_saveProjectButton);
    connect(m_saveProjectButton, &QPushButton::clicked, this, [this]() {
        saveProjectState();
    });

    m_rightTabs->addTab(wrapTabInScrollArea(cameraPanel), QStringLiteral("Global"));
    
    // Store reference to parallel load checkbox for persistence (optional)
    // Note: This would need a member variable to be added to shell.h if we want to persist the setting
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
    if (m_viewportCountCombo)
    {
        const int index = m_viewportCountCombo->findData(m_viewportHost->viewportCount());
        if (index >= 0)
        {
            m_viewportCountCombo->setCurrentIndex(index);
        }
    }
    const motive::PhysicsSettings physicsSettings = m_viewportHost->globalPhysicsSettings();
    if (m_globalPhysicsEngineCombo)
    {
        const int index = m_globalPhysicsEngineCombo->findData(static_cast<int>(physicsSettings.engineType));
        if (index >= 0)
        {
            m_globalPhysicsEngineCombo->setCurrentIndex(index);
        }
    }
    if (m_globalGravityXSpin)
    {
        m_globalGravityXSpin->setValue(physicsSettings.gravity.x);
    }
    if (m_globalGravityYSpin)
    {
        m_globalGravityYSpin->setValue(physicsSettings.gravity.y);
    }
    if (m_globalGravityZSpin)
    {
        m_globalGravityZSpin->setValue(physicsSettings.gravity.z);
    }
    if (m_globalPhysicsMaxSubStepsSpin)
    {
        m_globalPhysicsMaxSubStepsSpin->setValue(physicsSettings.maxSubSteps);
    }
    if (m_globalPhysicsAutoSyncCheck)
    {
        m_globalPhysicsAutoSyncCheck->setChecked(physicsSettings.autoSync);
    }
    if (m_globalPhysicsDebugDrawCheck)
    {
        m_globalPhysicsDebugDrawCheck->setChecked(physicsSettings.debugDraw);
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
    
    m_updatingCameraSettings = false;
}

void MainWindowShell::applyCameraSettings()
{
    saveProjectState();
}

}  // namespace motive::ui
