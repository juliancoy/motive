#include "shell.h"
#include "host_widget.h"
#include "camera_follow_settings.h"

#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
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
    const auto setElementDetailTab = [this](int index)
    {
        if (!m_elementDetailTabs || index < 0 || index >= m_elementDetailTabs->count())
        {
            return;
        }
        m_elementDetailTabs->setCurrentIndex(index);
    };
    const auto setLightInspectorVisible = [this](bool visible)
    {
        if (m_lightSection) {
            m_lightSection->setVisible(true);
            m_lightSection->setEnabled(visible);
        }
        if (m_lightTypeCombo) {
            m_lightTypeCombo->setVisible(true);
            m_lightTypeCombo->setEnabled(visible);
        }
        if (m_lightBrightnessSpin) {
            m_lightBrightnessSpin->setVisible(true);
            m_lightBrightnessSpin->setEnabled(visible);
        }
        if (m_lightColorContainer) {
            m_lightColorContainer->setVisible(true);
            m_lightColorContainer->setEnabled(visible);
        }
    };
    const auto setFollowTargetInspectorVisible = [this](bool visible, int currentTargetIndex = -1)
    {
        if (m_cameraSection) {
            m_cameraSection->setVisible(true);
            m_cameraSection->setEnabled(visible);
        }
        if (m_followTargetCombo) {
            m_followTargetCombo->setVisible(true);
            m_followTargetCombo->setEnabled(visible);
            // Repopulate the combo box with current scene items
            m_followTargetCombo->blockSignals(true);
            m_followTargetCombo->clear();
            m_followTargetCombo->addItem(QStringLiteral("None (Free Camera)"), -1);
            for (int i = 0; i < m_sceneItems.size(); ++i) {
                QString name = m_sceneItems[i].name;
                if (name.isEmpty()) {
                    name = QStringLiteral("Scene Item %1").arg(i);
                }
                m_followTargetCombo->addItem(name, i);
            }
            // Select the current target
            int index = m_followTargetCombo->findData(currentTargetIndex);
            if (index >= 0) {
                m_followTargetCombo->setCurrentIndex(index);
            }
            m_followTargetCombo->blockSignals(false);
        }
        if (m_followTargetLabel) {
            m_followTargetLabel->setVisible(true);
            m_followTargetLabel->setEnabled(visible);
        }
    };
    const auto setFollowParamsVisible = [this](bool visible)
    {
        if (m_followParamsLabel) {
            m_followParamsLabel->setVisible(visible);
            m_followParamsLabel->setEnabled(visible);
        }
        if (m_followDistanceSpin) {
            m_followDistanceSpin->setVisible(visible);
            m_followDistanceSpin->setEnabled(visible);
        }
        if (m_followYawSpin) {
            m_followYawSpin->setVisible(visible);
            m_followYawSpin->setEnabled(visible);
        }
        if (m_followPitchSpin) {
            m_followPitchSpin->setVisible(visible);
            m_followPitchSpin->setEnabled(visible);
        }
        if (m_followSmoothSpin) {
            m_followSmoothSpin->setVisible(visible);
            m_followSmoothSpin->setEnabled(visible);
        }
    };
    const auto setTransformInspectorVisible = [this](bool visible)
    {
        if (m_transformSection) {
            m_transformSection->setVisible(true);
            m_transformSection->setEnabled(visible);
        }
        if (m_translationWidget) {
            m_translationWidget->setVisible(true);
            m_translationWidget->setEnabled(visible);
        }
        if (m_rotationWidget) {
            m_rotationWidget->setVisible(true);
            m_rotationWidget->setEnabled(visible);
        }
        if (m_scaleWidget) {
            m_scaleWidget->setVisible(true);
            m_scaleWidget->setEnabled(visible);
        }
    };
    const auto setPrimitiveInspectorVisible = [this](bool visible, const QString& cullMode = QStringLiteral("back"))
    {
        if (m_primitiveCullModeCombo)
        {
            m_primitiveCullModeCombo->setVisible(true);
            m_primitiveCullModeCombo->setEnabled(visible);
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
            m_primitiveForceAlphaButton->setVisible(true);
            m_primitiveForceAlphaButton->setEnabled(visible);
        }
        if (m_paintOverrideCheck)
        {
            m_paintOverrideCheck->setVisible(true);
            m_paintOverrideCheck->setEnabled(visible);
        }
        if (m_paintColorContainer)
        {
            m_paintColorContainer->setVisible(true);
            m_paintColorContainer->setEnabled(visible);
        }
    };
    const auto setLoadInspectorVisible = [this](bool visible, bool meshConsolidationEnabled = true)
    {
        if (m_materialSection) {
            m_materialSection->setVisible(true);
            m_materialSection->setEnabled(visible);
        }
        if (m_loadMeshConsolidationCheck)
        {
            m_loadMeshConsolidationCheck->setVisible(true);
            m_loadMeshConsolidationCheck->setEnabled(visible);
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
        if (m_animationSection)
        {
            m_animationSection->setVisible(true);
            m_animationSection->setEnabled(visible);
        }
        if (m_animationControlsWidget)
        {
            m_animationControlsWidget->setVisible(true);
            m_animationControlsWidget->setEnabled(visible);
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
        if (m_physicsSection) {
            m_physicsSection->setVisible(true);
            m_physicsSection->setEnabled(visible);
        }
        if (m_elementUseGravityCheck)
        {
            m_elementUseGravityCheck->setVisible(true);
            m_elementUseGravityCheck->setEnabled(visible);
            if (visible)
            {
                m_elementUseGravityCheck->blockSignals(true);
                m_elementUseGravityCheck->setChecked(useGravity);
                m_elementUseGravityCheck->blockSignals(false);
            }
        }
        if (m_elementGravityWidget)
        {
            m_elementGravityWidget->setVisible(true);
            m_elementGravityWidget->setEnabled(visible);
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
    const auto setCharacterMotionInspector = [this](bool visible, float turnResponsiveness = 10.0f)
    {
        if (!m_characterTurnResponsivenessSpin)
        {
            return;
        }
        m_characterTurnResponsivenessSpin->setVisible(true);
        m_characterTurnResponsivenessSpin->setEnabled(visible);
        if (visible)
        {
            m_characterTurnResponsivenessSpin->blockSignals(true);
            m_characterTurnResponsivenessSpin->setValue(turnResponsiveness);
            m_characterTurnResponsivenessSpin->blockSignals(false);
        }
    };
    const auto setObjectRuntimeInspector = [this](bool visible,
                                                  const QString& followCamInfo = QStringLiteral("-"),
                                                  const QString& kinematicInfo = QStringLiteral("-"),
                                                  const QString& animationInfo = QStringLiteral("-"))
    {
        if (m_runtimeSection) {
            m_runtimeSection->setVisible(true);
            m_runtimeSection->setEnabled(visible);
        }
        if (m_objectFollowCamInfoValue)
        {
            m_objectFollowCamInfoValue->setVisible(true);
            m_objectFollowCamInfoValue->setEnabled(visible);
            m_objectFollowCamInfoValue->setText(followCamInfo);
        }
        if (m_objectKinematicInfoValue)
        {
            m_objectKinematicInfoValue->setVisible(true);
            m_objectKinematicInfoValue->setEnabled(visible);
            m_objectKinematicInfoValue->setText(kinematicInfo);
        }
        if (m_objectAnimationRuntimeInfoValue)
        {
            m_objectAnimationRuntimeInfoValue->setVisible(true);
            m_objectAnimationRuntimeInfoValue->setEnabled(visible);
            m_objectAnimationRuntimeInfoValue->setText(animationInfo);
        }
    };
    const auto setMotionOverlayInspector = [this](bool visible)
    {
        if (m_motionDebugOverlaySection)
        {
            m_motionDebugOverlaySection->setVisible(true);
            m_motionDebugOverlaySection->setEnabled(visible);
        }
        if (!visible || !m_viewportHost)
        {
            return;
        }

        const QJsonObject options = m_viewportHost->motionDebugOverlayOptionsJson();
        const bool enabled = options.value(QStringLiteral("enabled")).toBool(false);
        const bool drawTargetMarkers = options.value(QStringLiteral("showTargetMarkers")).toBool(true);
        const bool drawVelocity = options.value(QStringLiteral("showVelocityVector")).toBool(true);
        const bool drawCameraLine = options.value(QStringLiteral("showCameraToTargetLine")).toBool(true);
        const bool drawCenterCrosshair = options.value(QStringLiteral("showScreenCenterCrosshair")).toBool(true);
        const double velocityScale = options.value(QStringLiteral("velocityScale")).toDouble(0.25);

        if (m_motionOverlayEnabledCheck)
        {
            m_motionOverlayEnabledCheck->blockSignals(true);
            m_motionOverlayEnabledCheck->setChecked(enabled);
            m_motionOverlayEnabledCheck->blockSignals(false);
        }
        if (m_motionOverlayTargetMarkersCheck)
        {
            m_motionOverlayTargetMarkersCheck->blockSignals(true);
            m_motionOverlayTargetMarkersCheck->setChecked(drawTargetMarkers);
            m_motionOverlayTargetMarkersCheck->blockSignals(false);
        }
        if (m_motionOverlayVelocityCheck)
        {
            m_motionOverlayVelocityCheck->blockSignals(true);
            m_motionOverlayVelocityCheck->setChecked(drawVelocity);
            m_motionOverlayVelocityCheck->blockSignals(false);
        }
        if (m_motionOverlayCameraLineCheck)
        {
            m_motionOverlayCameraLineCheck->blockSignals(true);
            m_motionOverlayCameraLineCheck->setChecked(drawCameraLine);
            m_motionOverlayCameraLineCheck->blockSignals(false);
        }
        if (m_motionOverlayCenterCrosshairCheck)
        {
            m_motionOverlayCenterCrosshairCheck->blockSignals(true);
            m_motionOverlayCenterCrosshairCheck->setChecked(drawCenterCrosshair);
            m_motionOverlayCenterCrosshairCheck->blockSignals(false);
        }
        if (m_motionOverlayVelocityScaleSpin)
        {
            m_motionOverlayVelocityScaleSpin->blockSignals(true);
            m_motionOverlayVelocityScaleSpin->setValue(velocityScale);
            m_motionOverlayVelocityScaleSpin->blockSignals(false);
        }
    };
    const auto vec3FromJson = [](const QJsonValue& value) -> QVector3D
    {
        const QJsonArray arr = value.toArray();
        if (arr.size() < 3)
        {
            return QVector3D(0.0f, 0.0f, 0.0f);
        }
        return QVector3D(static_cast<float>(arr.at(0).toDouble()),
                         static_cast<float>(arr.at(1).toDouble()),
                         static_cast<float>(arr.at(2).toDouble()));
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
    const auto resolveCameraIndex = [this](QTreeWidgetItem* item) -> int
    {
        if (!item || !m_viewportHost)
        {
            return -1;
        }

        const QString cameraId = item->data(0, Qt::UserRole + 6).toString();
        if (!cameraId.isEmpty())
        {
            const int indexById = m_viewportHost->cameraIndexForId(cameraId);
            if (indexById >= 0)
            {
                return indexById;
            }
        }

        return item->data(0, Qt::UserRole + 5).toInt();
    };

    if (!valid)
    {
        // Handle Camera and Follow Camera entries (any row <= kHierarchyCameraIndex)
        if (row <= MainWindowShell::kHierarchyCameraIndex && m_viewportHost)
        {
            QTreeWidgetItem* currentItem = m_hierarchyTree ? m_hierarchyTree->currentItem() : nullptr;
            int cameraIndex = resolveCameraIndex(currentItem);
            if (cameraIndex < 0)
            {
                cameraIndex = 0;
            }
            
            // Get camera config to determine type
            auto configs = m_viewportHost->cameraConfigs();
            bool isFollowCamera = false;
            int followTargetIndex = -1;
            float followDistance = 5.0f;
            float followYaw = 0.0f;
            float followPitch = 20.0f;
            float followSmooth = followcam::kDefaultSmoothSpeed;
            float nearClip = 0.1f;
            float farClip = 100.0f;
            bool invertHorizontalDrag = false;
            
            if (cameraIndex >= 0 && cameraIndex < configs.size()) {
                isFollowCamera = configs[cameraIndex].isFollowCamera();
                followTargetIndex = configs[cameraIndex].followTargetIndex;
                followDistance = configs[cameraIndex].followDistance;
                followYaw = configs[cameraIndex].followYaw;
                followPitch = configs[cameraIndex].followPitch;
                followSmooth = configs[cameraIndex].followSmoothSpeed;
                nearClip = configs[cameraIndex].nearClip;
                farClip = configs[cameraIndex].farClip;
                invertHorizontalDrag = configs[cameraIndex].invertHorizontalDrag;
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
            setValue(m_cameraTypeValue, isFollowCamera ? QStringLiteral("Follow Camera")
                                                       : QStringLiteral("Free Camera"));
            setValue(m_boundsSizeValue, QStringLiteral("-"));
            setElementDetailTab(3); // Camera
            
            // Show free fly checkbox for all cameras
            if (m_freeFlyCameraCheck) {
                m_freeFlyCameraCheck->setVisible(true);
                m_freeFlyCameraCheck->setEnabled(true);
                bool freeFly = true;
                QString mode = QStringLiteral("FreeFly");
                if (cameraIndex >= 0 && cameraIndex < configs.size()) {
                    mode = configs[cameraIndex].mode;
                    freeFly = mode.compare(QStringLiteral("FreeFly"), Qt::CaseInsensitive) == 0;
                }
                m_freeFlyCameraCheck->blockSignals(true);
                m_freeFlyCameraCheck->setChecked(freeFly);
                m_freeFlyCameraCheck->blockSignals(false);
                if (m_wasdRoutingCombo)
                {
                    const int index = m_wasdRoutingCombo->findData(mode);
                    m_wasdRoutingCombo->blockSignals(true);
                    m_wasdRoutingCombo->setVisible(true);
                    m_wasdRoutingCombo->setEnabled(true);
                    if (index >= 0)
                    {
                        m_wasdRoutingCombo->setCurrentIndex(index);
                    }
                    m_wasdRoutingCombo->blockSignals(false);
                }
            }
            if (m_invertHorizontalDragCheck) {
                m_invertHorizontalDragCheck->setVisible(true);
                m_invertHorizontalDragCheck->setEnabled(true);
                m_invertHorizontalDragCheck->blockSignals(true);
                m_invertHorizontalDragCheck->setChecked(invertHorizontalDrag);
                m_invertHorizontalDragCheck->blockSignals(false);
            }
            
            // Show near/far clip spinboxes for all cameras
            if (m_nearClipSpin) {
                m_nearClipSpin->setVisible(true);
                m_nearClipSpin->setEnabled(true);
                m_nearClipSpin->blockSignals(true);
                m_nearClipSpin->setValue(nearClip);
                m_nearClipSpin->blockSignals(false);
            }
            if (m_farClipSpin) {
                m_farClipSpin->setVisible(true);
                m_farClipSpin->setEnabled(true);
                m_farClipSpin->blockSignals(true);
                m_farClipSpin->setValue(farClip);
                m_farClipSpin->blockSignals(false);
            }
            
            // For free cameras: show transform, hide follow params
            // For follow cameras: show follow params, hide transform (follow cameras don't have manual translation/rotation/scale)
            if (isFollowCamera) {
                // Show follow params with current values
                setFollowParamsVisible(true);
                if (m_followDistanceSpin) m_followDistanceSpin->setValue(followDistance);
                if (m_followYawSpin) m_followYawSpin->setValue(followYaw);
                if (m_followPitchSpin) m_followPitchSpin->setValue(followPitch);
                if (m_followSmoothSpin) m_followSmoothSpin->setValue(followSmooth);
                
                // Keep transform controls visible but disabled for follow cameras.
                setTransformInspectorVisible(true);
                if (m_lockScaleXYZCheck) {
                    m_lockScaleXYZCheck->setVisible(true);
                    m_lockScaleXYZCheck->setEnabled(false);
                }
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
                // Free camera: hide follow params, show transform (but hide scale - cameras don't have scale)
                setFollowParamsVisible(false);
                setTransformInspectorVisible(true);
                
                // Enable translation and rotation controls
                if (m_inspectorTranslationX) m_inspectorTranslationX->setEnabled(true);
                if (m_inspectorTranslationY) m_inspectorTranslationY->setEnabled(true);
                if (m_inspectorTranslationZ) m_inspectorTranslationZ->setEnabled(true);
                if (m_inspectorRotationX) m_inspectorRotationX->setEnabled(true);
                if (m_inspectorRotationY) m_inspectorRotationY->setEnabled(true);
                if (m_inspectorRotationZ) m_inspectorRotationZ->setEnabled(true);
                
                // Disable scale controls for cameras (cameras don't have scale)
                if (m_inspectorScaleX) m_inspectorScaleX->setEnabled(false);
                if (m_inspectorScaleY) m_inspectorScaleY->setEnabled(false);
                if (m_inspectorScaleZ) m_inspectorScaleZ->setEnabled(false);
                if (m_lockScaleXYZCheck) {
                    m_lockScaleXYZCheck->setVisible(true);
                    m_lockScaleXYZCheck->setEnabled(false);
                }
                
                QVector3D pos = m_viewportHost->cameraPosition();
                QVector3D rot = m_viewportHost->cameraRotation();
                if (m_inspectorTranslationX) m_inspectorTranslationX->setValue(pos.x());
                if (m_inspectorTranslationY) m_inspectorTranslationY->setValue(pos.y());
                if (m_inspectorTranslationZ) m_inspectorTranslationZ->setValue(pos.z());
                if (m_inspectorRotationX) m_inspectorRotationX->setValue(rot.x());
                if (m_inspectorRotationY) m_inspectorRotationY->setValue(rot.y());
                if (m_inspectorRotationZ) m_inspectorRotationZ->setValue(rot.z());
                
                // Set scale to 1.0 but disabled (cameras don't have scale)
                if (m_inspectorScaleX) m_inspectorScaleX->setValue(1.0);
                if (m_inspectorScaleY) m_inspectorScaleY->setValue(1.0);
                if (m_inspectorScaleZ) m_inspectorScaleZ->setValue(1.0);
            }
            setLoadInspectorVisible(false);
            if (m_primitiveForceAlphaButton) m_primitiveForceAlphaButton->setChecked(false);
            if (m_paintOverrideCheck) m_paintOverrideCheck->setChecked(false);
            setPrimitiveInspectorVisible(false);
            setLightInspectorVisible(false);
            setAnimationInspector(false);
            setGravityInspector(false);
            setCharacterMotionInspector(false);
            setObjectRuntimeInspector(false);
            setMotionOverlayInspector(true);
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
            setValue(m_cameraTypeValue, QStringLiteral("-"));
            setValue(m_boundsSizeValue, QStringLiteral("-"));
            setElementDetailTab(1); // Visual
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
            setCharacterMotionInspector(false);
            setObjectRuntimeInspector(false);
            setMotionOverlayInspector(true);
            if (m_lockScaleXYZCheck) {
                m_lockScaleXYZCheck->setVisible(true);
                m_lockScaleXYZCheck->setEnabled(false);
            }
            if (m_freeFlyCameraCheck) {
                m_freeFlyCameraCheck->setVisible(true);
                m_freeFlyCameraCheck->setEnabled(false);
            }
            if (m_wasdRoutingCombo) {
                m_wasdRoutingCombo->setVisible(true);
                m_wasdRoutingCombo->setEnabled(false);
            }
            if (m_invertHorizontalDragCheck) {
                m_invertHorizontalDragCheck->setVisible(true);
                m_invertHorizontalDragCheck->setEnabled(false);
            }
            if (m_nearClipSpin) {
                m_nearClipSpin->setVisible(true);
                m_nearClipSpin->setEnabled(false);
            }
            if (m_farClipSpin) {
                m_farClipSpin->setVisible(true);
                m_farClipSpin->setEnabled(false);
            }
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
        if (m_freeFlyCameraCheck) {
            m_freeFlyCameraCheck->setVisible(true);
            m_freeFlyCameraCheck->setEnabled(false);
        }
        if (m_wasdRoutingCombo) {
            m_wasdRoutingCombo->setVisible(true);
            m_wasdRoutingCombo->setEnabled(false);
        }
        if (m_invertHorizontalDragCheck) {
            m_invertHorizontalDragCheck->setVisible(true);
            m_invertHorizontalDragCheck->setEnabled(false);
        }
        if (m_nearClipSpin) {
            m_nearClipSpin->setVisible(true);
            m_nearClipSpin->setEnabled(false);
        }
        if (m_farClipSpin) {
            m_farClipSpin->setVisible(true);
            m_farClipSpin->setEnabled(false);
        }
        setValue(m_inspectorNameValue, QStringLiteral("-"));
        setValue(m_inspectorPathValue, QStringLiteral("-"));
        setValue(m_animationModeValue, QStringLiteral("-"));
        setValue(m_cameraTypeValue, QStringLiteral("-"));
        setValue(m_boundsSizeValue, QStringLiteral("-"));
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
        setCharacterMotionInspector(false);
        setObjectRuntimeInspector(false);
        setMotionOverlayInspector(true);
        if (m_lockScaleXYZCheck) {
            m_lockScaleXYZCheck->setVisible(true);
            m_lockScaleXYZCheck->setEnabled(false);
        }
        setTexturePreview(QImage());
        m_updatingInspector = false;
        return;
    }

    const auto& item = m_sceneItems[row];
    setElementDetailTab(0); // Overview
    setValue(m_inspectorNameValue, item.name);
    setValue(m_inspectorPathValue, QDir::toNativeSeparators(item.sourcePath));
    setValue(m_animationModeValue,
             m_viewportHost ? m_viewportHost->animationExecutionMode(row, meshIndex, primitiveIndex)
                            : QStringLiteral("-"));
    if (m_boundsSizeValue && m_viewportHost)
    {
        const QVector3D bounds = m_viewportHost->sceneItemBoundsSize(row);
        setValue(m_boundsSizeValue, QStringLiteral("%1 x %2 x %3")
                 .arg(QString::number(bounds.x(), 'f', 3))
                 .arg(QString::number(bounds.y(), 'f', 3))
                 .arg(QString::number(bounds.z(), 'f', 3)));
    }
    if (m_inspectorTranslationX) m_inspectorTranslationX->setValue(item.translation.x());
    if (m_inspectorTranslationY) m_inspectorTranslationY->setValue(item.translation.y());
    if (m_inspectorTranslationZ) m_inspectorTranslationZ->setValue(item.translation.z());
    if (m_inspectorRotationX) m_inspectorRotationX->setValue(item.rotation.x());
    if (m_inspectorRotationY) m_inspectorRotationY->setValue(item.rotation.y());
    if (m_inspectorRotationZ) m_inspectorRotationZ->setValue(item.rotation.z());
    if (m_inspectorScaleX) m_inspectorScaleX->setValue(item.scale.x());
    if (m_inspectorScaleY) m_inspectorScaleY->setValue(item.scale.y());
    if (m_inspectorScaleZ) m_inspectorScaleZ->setValue(item.scale.z());
    // Scene items must always re-enable transform controls after camera/light selections
    // may have disabled them.
    setTransformInspectorVisible(true);
    if (m_inspectorTranslationX) m_inspectorTranslationX->setEnabled(true);
    if (m_inspectorTranslationY) m_inspectorTranslationY->setEnabled(true);
    if (m_inspectorTranslationZ) m_inspectorTranslationZ->setEnabled(true);
    if (m_inspectorRotationX) m_inspectorRotationX->setEnabled(true);
    if (m_inspectorRotationY) m_inspectorRotationY->setEnabled(true);
    if (m_inspectorRotationZ) m_inspectorRotationZ->setEnabled(true);
    if (m_inspectorScaleX) m_inspectorScaleX->setEnabled(true);
    if (m_inspectorScaleY) m_inspectorScaleY->setEnabled(true);
    if (m_inspectorScaleZ) m_inspectorScaleZ->setEnabled(true);
    if (m_lockScaleXYZCheck) {
        m_lockScaleXYZCheck->setVisible(true);
        m_lockScaleXYZCheck->setEnabled(true);
    }
    if (m_paintOverrideCheck) m_paintOverrideCheck->setChecked(item.paintOverrideEnabled);
    if (m_paintColorWidget)
    {
        const QColor color = QColor::fromRgbF(item.paintOverrideColor.x(), item.paintOverrideColor.y(), item.paintOverrideColor.z());
        m_paintColorWidget->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid #888;").arg(color.name()));
        m_paintColorWidget->setProperty("paintColor", color.name());
    }
    const QString suffix = QFileInfo(item.sourcePath).suffix().toLower();
    const bool loadVisible = suffix == QStringLiteral("gltf") || suffix == QStringLiteral("glb");
    const bool primitiveControlsVisible = m_viewportHost && meshIndex >= 0 && primitiveIndex >= 0;
    setLoadInspectorVisible(loadVisible, item.meshConsolidationEnabled);
    setPrimitiveInspectorVisible(primitiveControlsVisible,
                                 m_viewportHost ? m_viewportHost->primitiveCullMode(row, meshIndex, primitiveIndex) : QStringLiteral("back"));
    if (m_materialSection)
    {
        m_materialSection->setVisible(true);
        m_materialSection->setEnabled(loadVisible || primitiveControlsVisible);
    }
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
    int followCameraIndex = -1;
    bool hasObjectFollowCamera = false;
    ViewportHostWidget::CameraConfig objectFollowConfig;
    if (m_viewportHost)
    {
        const auto configs = m_viewportHost->cameraConfigs();
        for (int i = 0; i < configs.size(); ++i)
        {
            const auto& cfg = configs[i];
            if (cfg.isFollowCamera() && cfg.followTargetIndex == row)
            {
                followCameraIndex = i;
                hasObjectFollowCamera = true;
                objectFollowConfig = cfg;
                break;
            }
        }
    }
    setFollowTargetInspectorVisible(true, hasObjectFollowCamera ? row : -1);
    setValue(m_cameraTypeValue, hasObjectFollowCamera ? QStringLiteral("Object Follow Camera")
                                                      : QStringLiteral("None"));
    if (m_freeFlyCameraCheck) {
        m_freeFlyCameraCheck->setVisible(true);
    }
    if (m_wasdRoutingCombo) {
        m_wasdRoutingCombo->setVisible(true);
    }
    if (m_invertHorizontalDragCheck) {
        m_invertHorizontalDragCheck->setVisible(true);
    }
    if (m_nearClipSpin) {
        m_nearClipSpin->setVisible(true);
    }
    if (m_farClipSpin) {
        m_farClipSpin->setVisible(true);
    }
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
    setCharacterMotionInspector(true, item.characterTurnResponsiveness);
    if (m_viewportHost)
    {
        QString followCamInfo = QStringLiteral("None");
        if (hasObjectFollowCamera)
        {
            const auto& cfg = objectFollowConfig;
            const QString followCamName = cfg.name.isEmpty()
                ? QStringLiteral("Follow Cam (Scene %1)").arg(row)
                : cfg.name;
            followCamInfo = QStringLiteral("%1\nDistance: %2  Yaw: %3  Pitch: %4  Damping: %5")
                .arg(followCamName)
                .arg(QString::number(cfg.followDistance, 'f', 2))
                .arg(QString::number(cfg.followYaw, 'f', 1))
                .arg(QString::number(cfg.followPitch, 'f', 1))
                .arg(QString::number(cfg.followSmoothSpeed, 'f', 2));
        }
        if (m_followTargetCombo)
        {
            m_followTargetCombo->setEnabled(true);
        }
        setFollowParamsVisible(hasObjectFollowCamera);
        if (m_freeFlyCameraCheck)
        {
            m_freeFlyCameraCheck->setEnabled(hasObjectFollowCamera);
        }
        if (m_wasdRoutingCombo)
        {
            m_wasdRoutingCombo->setEnabled(hasObjectFollowCamera);
        }
        if (m_invertHorizontalDragCheck)
        {
            m_invertHorizontalDragCheck->setEnabled(hasObjectFollowCamera);
        }
        if (m_nearClipSpin)
        {
            m_nearClipSpin->setEnabled(hasObjectFollowCamera);
        }
        if (m_farClipSpin)
        {
            m_farClipSpin->setEnabled(hasObjectFollowCamera);
        }
        if (hasObjectFollowCamera)
        {
            if (m_followDistanceSpin) m_followDistanceSpin->setValue(objectFollowConfig.followDistance);
            if (m_followYawSpin) m_followYawSpin->setValue(objectFollowConfig.followYaw);
            if (m_followPitchSpin) m_followPitchSpin->setValue(objectFollowConfig.followPitch);
            if (m_followSmoothSpin) m_followSmoothSpin->setValue(objectFollowConfig.followSmoothSpeed);
            if (m_freeFlyCameraCheck)
            {
                m_freeFlyCameraCheck->setChecked(
                    objectFollowConfig.mode.compare(QStringLiteral("FreeFly"), Qt::CaseInsensitive) == 0);
            }
            if (m_wasdRoutingCombo)
            {
                const int modeIndex = m_wasdRoutingCombo->findData(objectFollowConfig.mode);
                if (modeIndex >= 0) m_wasdRoutingCombo->setCurrentIndex(modeIndex);
            }
            if (m_invertHorizontalDragCheck) m_invertHorizontalDragCheck->setChecked(objectFollowConfig.invertHorizontalDrag);
            if (m_nearClipSpin) m_nearClipSpin->setValue(objectFollowConfig.nearClip);
            if (m_farClipSpin) m_farClipSpin->setValue(objectFollowConfig.farClip);
        }
        else
        {
            if (m_followDistanceSpin) m_followDistanceSpin->setValue(5.0);
            if (m_followYawSpin) m_followYawSpin->setValue(0.0);
            if (m_followPitchSpin) m_followPitchSpin->setValue(20.0);
            if (m_followSmoothSpin) m_followSmoothSpin->setValue(followcam::kDefaultSmoothSpeed);
            if (m_freeFlyCameraCheck) m_freeFlyCameraCheck->setChecked(false);
            if (m_wasdRoutingCombo) m_wasdRoutingCombo->setCurrentIndex(m_wasdRoutingCombo->findData(QStringLiteral("OrbitFollow")));
            if (m_invertHorizontalDragCheck) m_invertHorizontalDragCheck->setChecked(false);
            if (m_nearClipSpin) m_nearClipSpin->setValue(0.1);
            if (m_farClipSpin) m_farClipSpin->setValue(100.0);
        }

        QString kinematicInfo = QStringLiteral("Coupling: %1")
            .arg(item.animationPhysicsCoupling.isEmpty()
                     ? QStringLiteral("AnimationOnly")
                     : item.animationPhysicsCoupling);
        QString runtimeClip = selectedClip;
        bool runtimePlaying = item.animationPlaying;
        bool runtimeLoop = item.animationLoop;
        double runtimeSpeed = item.animationSpeed;
        QString animationRuntimeInfo = QStringLiteral("Configured Clip: %1 | Playing: %2 | Loop: %3 | Speed: %4")
            .arg(runtimeClip.isEmpty() ? QStringLiteral("-") : runtimeClip)
            .arg(runtimePlaying ? QStringLiteral("true") : QStringLiteral("false"))
            .arg(runtimeLoop ? QStringLiteral("true") : QStringLiteral("false"))
            .arg(QString::number(runtimeSpeed, 'f', 2));

        const QJsonArray profile = m_viewportHost->sceneProfileJson();
        if (row >= 0 && row < profile.size() && profile.at(row).isObject())
        {
            const QJsonObject runtime = profile.at(row).toObject();
            const bool isControllable = runtime.value(QStringLiteral("isControllable")).toBool(false);
            const bool isGrounded = runtime.value(QStringLiteral("isGrounded")).toBool(false);
            const bool jumpRequested = runtime.value(QStringLiteral("jumpRequested")).toBool(false);
            const QVector3D inputDir = vec3FromJson(runtime.value(QStringLiteral("inputDir")));
            const QVector3D velocity = vec3FromJson(runtime.value(QStringLiteral("velocity")));
            const QString currentAnimState = runtime.value(QStringLiteral("currentAnimState")).toString(QStringLiteral("Unknown"));
            const double currentAnimWeight = runtime.value(QStringLiteral("currentAnimWeight")).toDouble(0.0);
            const double currentAnimSpeed = runtime.value(QStringLiteral("currentAnimSpeed")).toDouble(0.0);
            const QString runtimeActiveClip = runtime.value(QStringLiteral("runtimeActiveClip")).toString();
            runtimeClip = runtimeActiveClip.isEmpty() ? runtimeClip : runtimeActiveClip;
            runtimePlaying = runtime.value(QStringLiteral("runtimeAnimationPlaying")).toBool(runtimePlaying);
            runtimeLoop = runtime.value(QStringLiteral("runtimeAnimationLoop")).toBool(runtimeLoop);
            runtimeSpeed = runtime.value(QStringLiteral("runtimeAnimationSpeed")).toDouble(runtimeSpeed);

            kinematicInfo = QStringLiteral(
                                "Coupling: %1\n"
                                "Controllable: %2  Grounded: %3  JumpReq: %4\n"
                                "InputDir: (%5, %6, %7)\n"
                                "Velocity: (%8, %9, %10)")
                                .arg(item.animationPhysicsCoupling.isEmpty()
                                         ? QStringLiteral("AnimationOnly")
                                         : item.animationPhysicsCoupling)
                                .arg(isControllable ? QStringLiteral("true") : QStringLiteral("false"))
                                .arg(isGrounded ? QStringLiteral("true") : QStringLiteral("false"))
                                .arg(jumpRequested ? QStringLiteral("true") : QStringLiteral("false"))
                                .arg(QString::number(inputDir.x(), 'f', 2))
                                .arg(QString::number(inputDir.y(), 'f', 2))
                                .arg(QString::number(inputDir.z(), 'f', 2))
                                .arg(QString::number(velocity.x(), 'f', 2))
                                .arg(QString::number(velocity.y(), 'f', 2))
                                .arg(QString::number(velocity.z(), 'f', 2));

            animationRuntimeInfo = QStringLiteral(
                                       "Configured Clip: %1 | Playing: %2 | Loop: %3 | Speed: %4\n"
                                       "State: %5  RuntimeSpeed: %6  BlendWeight: %7")
                                       .arg(runtimeClip.isEmpty() ? QStringLiteral("-") : runtimeClip)
                                       .arg(runtimePlaying ? QStringLiteral("true") : QStringLiteral("false"))
                                       .arg(runtimeLoop ? QStringLiteral("true") : QStringLiteral("false"))
                                       .arg(QString::number(runtimeSpeed, 'f', 2))
                                       .arg(currentAnimState)
                                       .arg(QString::number(currentAnimSpeed, 'f', 2))
                                       .arg(QString::number(currentAnimWeight, 'f', 2));
        }

        setObjectRuntimeInspector(true, followCamInfo, kinematicInfo, animationRuntimeInfo);
        setMotionOverlayInspector(true);
    }
    else
    {
        setObjectRuntimeInspector(false);
        setMotionOverlayInspector(false);
    }
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
