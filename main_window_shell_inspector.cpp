#include "main_window_shell.h"
#include "viewport_host_widget.h"

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
        if (row == MainWindowShell::kHierarchyCameraIndex && m_viewportHost)
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

}  // namespace motive::ui
