#include "shell.h"
#include "host_widget.h"

#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QTreeWidget>
#include <QTreeWidgetItem>

namespace motive::ui {

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
    const auto setFollowTargetInspectorVisible = [this](bool visible, int currentTargetIndex = -1)
    {
        if (m_followTargetCombo) {
            m_followTargetCombo->setVisible(visible);
            if (visible) {
                // Repopulate the combo box with current scene items
                m_followTargetCombo->blockSignals(true);
                m_followTargetCombo->clear();
                m_followTargetCombo->addItem(QStringLiteral("None (Free Camera)"), -1);
                for (int i = 0; i < m_sceneItems.size(); ++i) {
                    m_followTargetCombo->addItem(m_sceneItems[i].name, i);
                }
                // Select the current target
                int index = m_followTargetCombo->findData(currentTargetIndex);
                if (index >= 0) {
                    m_followTargetCombo->setCurrentIndex(index);
                }
                m_followTargetCombo->blockSignals(false);
            }
        }
        if (m_followTargetLabel) m_followTargetLabel->setVisible(visible);
    };
    const auto setFollowParamsVisible = [this](bool visible)
    {
        if (m_followParamsLabel) m_followParamsLabel->setVisible(visible);
        if (m_followDistanceSpin) m_followDistanceSpin->setVisible(visible);
        if (m_followYawSpin) m_followYawSpin->setVisible(visible);
        if (m_followPitchSpin) m_followPitchSpin->setVisible(visible);
        if (m_followSmoothSpin) m_followSmoothSpin->setVisible(visible);
    };
    const auto setTransformInspectorVisible = [this](bool visible)
    {
        // Find the label widget parent to hide the entire row
        if (m_inspectorTranslationX && m_inspectorTranslationX->parentWidget()) {
            QWidget* parent = m_inspectorTranslationX->parentWidget()->parentWidget();
            if (parent) parent->setVisible(visible);
        }
        if (m_inspectorRotationX && m_inspectorRotationX->parentWidget()) {
            QWidget* parent = m_inspectorRotationX->parentWidget()->parentWidget();
            if (parent) parent->setVisible(visible);
        }
        if (m_inspectorScaleX && m_inspectorScaleX->parentWidget()) {
            QWidget* parent = m_inspectorScaleX->parentWidget()->parentWidget();
            if (parent) parent->setVisible(visible);
        }
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
                                              float speed = 1.0f,
                                              const QString& physicsCoupling = QStringLiteral("AnimationOnly"))
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
        
        // Set physics coupling
        if (m_animationPhysicsCouplingCombo)
        {
            m_animationPhysicsCouplingCombo->blockSignals(true);
            int couplingIndex = m_animationPhysicsCouplingCombo->findData(physicsCoupling);
            if (couplingIndex >= 0)
            {
                m_animationPhysicsCouplingCombo->setCurrentIndex(couplingIndex);
            }
            m_animationPhysicsCouplingCombo->blockSignals(false);
        }
    };
    const auto setGravityInspector = [this](bool visible, bool useGravity = true, const QVector3D& customGravity = QVector3D(0.0f, 0.0f, 0.0f))
    {
        if (m_elementUseGravityCheck)
        {
            m_elementUseGravityCheck->setVisible(visible);
            if (visible)
            {
                m_elementUseGravityCheck->blockSignals(true);
                m_elementUseGravityCheck->setChecked(useGravity);
                m_elementUseGravityCheck->blockSignals(false);
            }
        }
        if (m_elementGravityWidget)
        {
            m_elementGravityWidget->setVisible(visible);
            if (visible)
            {
                if (m_elementGravityX)
                {
                    m_elementGravityX->blockSignals(true);
                    m_elementGravityX->setValue(customGravity.x());
                    m_elementGravityX->blockSignals(false);
                }
                if (m_elementGravityY)
                {
                    m_elementGravityY->blockSignals(true);
                    m_elementGravityY->setValue(customGravity.y());
                    m_elementGravityY->blockSignals(false);
                }
                if (m_elementGravityZ)
                {
                    m_elementGravityZ->blockSignals(true);
                    m_elementGravityZ->setValue(customGravity.z());
                    m_elementGravityZ->blockSignals(false);
                }
            }
        }
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
        // Handle Camera and Follow Camera entries (any row <= kHierarchyCameraIndex)
        if (row <= MainWindowShell::kHierarchyCameraIndex && m_viewportHost)
        {
            QTreeWidgetItem* currentItem = m_hierarchyTree ? m_hierarchyTree->currentItem() : nullptr;
            int cameraIndex = currentItem ? currentItem->data(0, Qt::UserRole + 5).toInt() : 0;
            
            // Get camera config to determine type
            auto configs = m_viewportHost->cameraConfigs();
            bool isFollowCamera = false;
            int followTargetIndex = -1;
            float followDistance = 5.0f;
            float followYaw = 0.0f;
            float followPitch = 20.0f;
            float followSmooth = 5.0f;
            
            if (cameraIndex >= 0 && cameraIndex < configs.size()) {
                isFollowCamera = configs[cameraIndex].isFollowCamera();
                followTargetIndex = configs[cameraIndex].followTargetIndex;
                followDistance = configs[cameraIndex].followDistance;
                followYaw = configs[cameraIndex].followYaw;
                followPitch = configs[cameraIndex].followPitch;
                followSmooth = configs[cameraIndex].followSmoothSpeed;
            }
            
            QString cameraName = QStringLiteral("Camera");
            QString cameraPath = QStringLiteral("Viewport Camera");
            
            if (isFollowCamera) {
                cameraName = QStringLiteral("Follow Cam (Scene %1)").arg(followTargetIndex);
                cameraPath = QStringLiteral("Follow Camera");
            }
            
            setValue(m_inspectorNameValue, cameraName);
            setValue(m_inspectorPathValue, cameraPath);
            setValue(m_animationModeValue, QStringLiteral("Static"));
            
            // For free cameras: show transform, hide follow params
            // For follow cameras: show follow params, disable transform
            if (isFollowCamera) {
                // Show follow params with current values
                setFollowParamsVisible(true);
                if (m_followDistanceSpin) m_followDistanceSpin->setValue(followDistance);
                if (m_followYawSpin) m_followYawSpin->setValue(followYaw);
                if (m_followPitchSpin) m_followPitchSpin->setValue(followPitch);
                if (m_followSmoothSpin) m_followSmoothSpin->setValue(followSmooth);
                
                // Disable transform for follow cameras (they're computed)
                if (m_inspectorTranslationX) m_inspectorTranslationX->setEnabled(false);
                if (m_inspectorTranslationY) m_inspectorTranslationY->setEnabled(false);
                if (m_inspectorTranslationZ) m_inspectorTranslationZ->setEnabled(false);
                if (m_inspectorRotationX) m_inspectorRotationX->setEnabled(false);
                if (m_inspectorRotationY) m_inspectorRotationY->setEnabled(false);
                if (m_inspectorRotationZ) m_inspectorRotationZ->setEnabled(false);
                if (m_inspectorScaleX) m_inspectorScaleX->setEnabled(false);
                if (m_inspectorScaleY) m_inspectorScaleY->setEnabled(false);
                if (m_inspectorScaleZ) m_inspectorScaleZ->setEnabled(false);
            } else {
                // Free camera: hide follow params, enable transform
                setFollowParamsVisible(false);
                
                if (m_inspectorTranslationX) m_inspectorTranslationX->setEnabled(true);
                if (m_inspectorTranslationY) m_inspectorTranslationY->setEnabled(true);
                if (m_inspectorTranslationZ) m_inspectorTranslationZ->setEnabled(true);
                if (m_inspectorRotationX) m_inspectorRotationX->setEnabled(true);
                if (m_inspectorRotationY) m_inspectorRotationY->setEnabled(true);
                if (m_inspectorRotationZ) m_inspectorRotationZ->setEnabled(true);
                if (m_inspectorScaleX) m_inspectorScaleX->setEnabled(true);
                if (m_inspectorScaleY) m_inspectorScaleY->setEnabled(true);
                if (m_inspectorScaleZ) m_inspectorScaleZ->setEnabled(true);
                
                QVector3D pos = m_viewportHost->cameraPosition();
                QVector3D rot = m_viewportHost->cameraRotation();
                if (m_inspectorTranslationX) m_inspectorTranslationX->setValue(pos.x());
                if (m_inspectorTranslationY) m_inspectorTranslationY->setValue(pos.y());
                if (m_inspectorTranslationZ) m_inspectorTranslationZ->setValue(pos.z());
                if (m_inspectorRotationX) m_inspectorRotationX->setValue(rot.x());
                if (m_inspectorRotationY) m_inspectorRotationY->setValue(rot.y());
                if (m_inspectorRotationZ) m_inspectorRotationZ->setValue(rot.z());
            }
            
            if (m_inspectorScaleX) m_inspectorScaleX->setValue(1.0);
            if (m_inspectorScaleY) m_inspectorScaleY->setValue(1.0);
            if (m_inspectorScaleZ) m_inspectorScaleZ->setValue(1.0);
            setLoadInspectorVisible(false);
            if (m_primitiveForceAlphaButton) m_primitiveForceAlphaButton->setChecked(false);
            if (m_paintOverrideCheck) m_paintOverrideCheck->setChecked(false);
            setPrimitiveInspectorVisible(false);
            setLightInspectorVisible(false);
            setAnimationInspector(false);
            setGravityInspector(false);
            setTexturePreview(QImage());
            
            // Show follow target selector for all cameras
            setFollowTargetInspectorVisible(true, followTargetIndex);
            
            m_updatingInspector = false;
            return;
        }
        if (row == MainWindowShell::kHierarchyLightIndex)
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
            setFollowTargetInspectorVisible(false);
            setAnimationInspector(false);
            setGravityInspector(false);
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
        setFollowTargetInspectorVisible(false);
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
    setFollowTargetInspectorVisible(false);
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
                          item.animationSpeed,
                          item.animationPhysicsCoupling);
    setGravityInspector(true, item.useGravity, item.customGravity);
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

}  // namespace motive::ui
