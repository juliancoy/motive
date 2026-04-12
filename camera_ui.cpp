#include "shell.h"
#include "host_widget.h"

#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
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
