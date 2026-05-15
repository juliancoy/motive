#include "shell.h"
#include "host_widget.h"
#include "camera_follow_settings.h"
#include "viewport_internal_utils.h"

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
#include <QVariant>

#include <algorithm>

namespace motive::ui {

namespace {

constexpr int kOverviewTabIndex = 0;
constexpr int kVisualTabIndex = 1;
constexpr int kMotionTabIndex = 2;
constexpr int kCameraTabIndex = 3;
constexpr int kAnimationTabIndex = 4;
constexpr int kRuntimeTabIndex = 5;

QString elementPanelLabelForNodeType(int nodeType)
{
    using NodeType = ViewportHostWidget::HierarchyNode::Type;
    switch (static_cast<NodeType>(nodeType))
    {
    case NodeType::Camera:
        return QStringLiteral("Camera Element");
    case NodeType::Light:
        return QStringLiteral("Light Element");
    case NodeType::SceneItem:
        return QStringLiteral("Object Element");
    case NodeType::Mesh:
        return QStringLiteral("Mesh Element");
    case NodeType::Primitive:
        return QStringLiteral("Primitive Element");
    case NodeType::Material:
        return QStringLiteral("Material Element");
    case NodeType::Texture:
        return QStringLiteral("Texture Element");
    case NodeType::AnimationGroup:
        return QStringLiteral("Animation Element");
    case NodeType::AnimationClip:
        return QStringLiteral("Clip Element");
    case NodeType::PendingSceneItem:
        return QStringLiteral("Pending Element");
    }
    return QStringLiteral("Element");
}

}  // namespace

void MainWindowShell::configureElementInspectorForSelection(int nodeType,
                                                           int sceneIndex,
                                                           int meshIndex,
                                                           int primitiveIndex,
                                                           bool hasAnimation,
                                                           bool isTextItem,
                                                           bool focusContextTab)
{
    Q_UNUSED(meshIndex);
    Q_UNUSED(primitiveIndex);

    if (m_rightTabs)
    {
        for (int i = 0; i < m_rightTabs->count(); ++i)
        {
            if (m_rightTabs->tabText(i).endsWith(QStringLiteral("Element")) ||
                m_rightTabs->tabText(i) == QStringLiteral("Element"))
            {
                m_rightTabs->setTabText(i, elementPanelLabelForNodeType(nodeType));
                break;
            }
        }
    }

    if (!m_elementDetailTabs)
    {
        return;
    }

    bool showOverview = true;
    bool showVisual = false;
    bool showMotion = false;
    bool showCamera = false;
    bool showAnimation = false;
    bool showRuntime = false;
    int preferredTab = kOverviewTabIndex;

    using NodeType = ViewportHostWidget::HierarchyNode::Type;
    switch (static_cast<NodeType>(nodeType))
    {
    case NodeType::Camera:
        showCamera = true;
        showRuntime = true;
        preferredTab = focusContextTab ? kCameraTabIndex : kOverviewTabIndex;
        break;
    case NodeType::Light:
        showVisual = true;
        preferredTab = focusContextTab ? kVisualTabIndex : kOverviewTabIndex;
        break;
    case NodeType::SceneItem:
        showVisual = true;
        showMotion = !isTextItem;
        showCamera = !isTextItem;
        showAnimation = hasAnimation;
        showRuntime = !isTextItem;
        preferredTab = kOverviewTabIndex;
        break;
    case NodeType::Mesh:
        showVisual = true;
        showRuntime = true;
        preferredTab = focusContextTab ? kVisualTabIndex : kOverviewTabIndex;
        break;
    case NodeType::Primitive:
        showVisual = true;
        showRuntime = true;
        preferredTab = focusContextTab ? kVisualTabIndex : kOverviewTabIndex;
        break;
    case NodeType::Material:
        showVisual = true;
        preferredTab = focusContextTab ? kVisualTabIndex : kOverviewTabIndex;
        break;
    case NodeType::Texture:
        showVisual = true;
        preferredTab = focusContextTab ? kVisualTabIndex : kOverviewTabIndex;
        break;
    case NodeType::AnimationGroup:
    case NodeType::AnimationClip:
        showAnimation = hasAnimation;
        showRuntime = true;
        preferredTab = focusContextTab && showAnimation ? kAnimationTabIndex : kOverviewTabIndex;
        break;
    case NodeType::PendingSceneItem:
        preferredTab = kOverviewTabIndex;
        break;
    }

    const bool hasSceneObject = sceneIndex >= 0 && sceneIndex < m_sceneItems.size();
    if (!hasSceneObject)
    {
        showAnimation = false;
    }

    const bool visibleByIndex[] = {
        showOverview,
        showVisual,
        showMotion,
        showCamera,
        showAnimation,
        showRuntime
    };
    for (int i = 0; i < m_elementDetailTabs->count() && i < 6; ++i)
    {
        m_elementDetailTabs->setTabVisible(i, visibleByIndex[i]);
    }

    if (!m_elementDetailTabs->isTabVisible(m_elementDetailTabs->currentIndex()) ||
        (focusContextTab && m_elementDetailTabs->isTabVisible(preferredTab)))
    {
        m_elementDetailTabs->setCurrentIndex(
            m_elementDetailTabs->isTabVisible(preferredTab) ? preferredTab : kOverviewTabIndex);
    }

    const bool sceneItemNode = nodeType == static_cast<int>(NodeType::SceneItem);
    const bool meshNode = nodeType == static_cast<int>(NodeType::Mesh);
    const bool primitiveNode = nodeType == static_cast<int>(NodeType::Primitive);
    const bool materialNode = nodeType == static_cast<int>(NodeType::Material);
    const bool textureNode = nodeType == static_cast<int>(NodeType::Texture);
    const bool animationNode = nodeType == static_cast<int>(NodeType::AnimationGroup) ||
                               nodeType == static_cast<int>(NodeType::AnimationClip);

    if (m_transformSection)
    {
        m_transformSection->setVisible(sceneItemNode || nodeType == static_cast<int>(NodeType::Camera));
    }
    if (m_placementSection)
    {
        m_placementSection->setVisible(sceneItemNode && !isTextItem);
    }
    if (m_textSection)
    {
        m_textSection->setVisible(sceneItemNode && isTextItem);
    }
    if (m_materialSection)
    {
        m_materialSection->setVisible(sceneItemNode || meshNode || primitiveNode || materialNode || textureNode);
    }
    if (m_animationSection)
    {
        m_animationSection->setVisible((sceneItemNode || animationNode) && hasAnimation);
    }
    if (m_physicsSection)
    {
        m_physicsSection->setVisible(sceneItemNode && !isTextItem);
    }
    if (m_cameraSection)
    {
        m_cameraSection->setVisible(sceneItemNode || nodeType == static_cast<int>(NodeType::Camera));
    }
    if (m_lightSection)
    {
        m_lightSection->setVisible(nodeType == static_cast<int>(NodeType::Light));
    }
    if (m_runtimeSection)
    {
        m_runtimeSection->setVisible(sceneItemNode || meshNode || primitiveNode || animationNode ||
                                     nodeType == static_cast<int>(NodeType::Camera));
    }
    if (m_motionDebugOverlaySection)
    {
        m_motionDebugOverlaySection->setVisible(sceneItemNode || nodeType == static_cast<int>(NodeType::Camera));
    }
}

void MainWindowShell::updateInspectorForSelection(QTreeWidgetItem* currentItem, bool focusContextTab)
{
    m_updatingInspector = true;
    if (m_viewportHost)
    {
        m_sceneItems = m_viewportHost->sceneItems();
    }
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
    const auto setBoundsSummary = [this, &setValue](int sceneIndex)
    {
        if (!m_viewportHost || sceneIndex < 0 || sceneIndex >= m_sceneItems.size())
        {
            setValue(m_boundsSizeValue, QStringLiteral("-"));
            setValue(m_boundsCenterValue, QStringLiteral("-"));
            setValue(m_boundsMinValue, QStringLiteral("-"));
            setValue(m_boundsMaxValue, QStringLiteral("-"));
            return;
        }

        const QVector3D size = m_viewportHost->sceneItemBoundsSize(sceneIndex);
        const QVector3D center = m_viewportHost->sceneItemBoundsCenter(sceneIndex);
        const QVector3D minPoint = m_viewportHost->sceneItemBoundsMin(sceneIndex);
        const QVector3D maxPoint = m_viewportHost->sceneItemBoundsMax(sceneIndex);
        setValue(m_boundsSizeValue, QStringLiteral("%1 x %2 x %3")
                 .arg(QString::number(size.x(), 'f', 3))
                 .arg(QString::number(size.y(), 'f', 3))
                 .arg(QString::number(size.z(), 'f', 3)));
        setValue(m_boundsCenterValue, QStringLiteral("%1, %2, %3")
                 .arg(QString::number(center.x(), 'f', 3))
                 .arg(QString::number(center.y(), 'f', 3))
                 .arg(QString::number(center.z(), 'f', 3)));
        setValue(m_boundsMinValue, QStringLiteral("%1, %2, %3")
                 .arg(QString::number(minPoint.x(), 'f', 3))
                 .arg(QString::number(minPoint.y(), 'f', 3))
                 .arg(QString::number(minPoint.z(), 'f', 3)));
        setValue(m_boundsMaxValue, QStringLiteral("%1, %2, %3")
                 .arg(QString::number(maxPoint.x(), 'f', 3))
                 .arg(QString::number(maxPoint.y(), 'f', 3))
                 .arg(QString::number(maxPoint.z(), 'f', 3)));
    };
    const auto setPlacementInspector = [this](int sourceSceneIndex)
    {
        if (!m_placementSection || !m_placementTargetCombo || !m_placementApplyButton || !m_placementLandmarksValue)
        {
            return;
        }

        const bool validSource = m_viewportHost &&
            sourceSceneIndex >= 0 &&
            sourceSceneIndex < m_sceneItems.size();

        m_placementSection->setVisible(true);
        m_placementSection->setEnabled(validSource);
        m_placementTargetCombo->setEnabled(validSource);
        if (m_placementLandmarkCombo)
        {
            m_placementLandmarkCombo->setEnabled(validSource);
        }
        m_placementApplyButton->setEnabled(validSource);

        if (!validSource)
        {
            m_placementTargetCombo->clear();
            m_placementLandmarksValue->setText(QStringLiteral("-"));
            return;
        }

        const QVariant previousTargetData = m_placementTargetCombo->currentData();
        const int previousTargetIndex = previousTargetData.isValid() ? previousTargetData.toInt() : -1;
        m_placementTargetCombo->blockSignals(true);
        m_placementTargetCombo->clear();
        for (int i = 0; i < m_sceneItems.size(); ++i)
        {
            if (i == sourceSceneIndex)
            {
                continue;
            }
            QString label = m_sceneItems[i].name;
            if (label.isEmpty())
            {
                label = QStringLiteral("Scene Item %1").arg(i);
            }
            m_placementTargetCombo->addItem(label, i);
        }
        int targetIndex = m_placementTargetCombo->findData(previousTargetIndex);
        if (targetIndex < 0)
        {
            targetIndex = 0;
        }
        if (targetIndex >= 0)
        {
            m_placementTargetCombo->setCurrentIndex(targetIndex);
        }
        m_placementTargetCombo->blockSignals(false);

        const QVariant selectedTargetData = m_placementTargetCombo->currentData();
        const int selectedTarget = selectedTargetData.isValid() ? selectedTargetData.toInt() : -1;
        if (selectedTarget < 0 || selectedTarget >= m_sceneItems.size())
        {
            m_placementLandmarksValue->setText(QStringLiteral("-"));
            m_placementApplyButton->setEnabled(false);
            return;
        }

        const QVector3D center = m_viewportHost->sceneItemBoundsCenter(selectedTarget);
        const QVector3D minPoint = m_viewportHost->sceneItemBoundsMin(selectedTarget);
        const QVector3D maxPoint = m_viewportHost->sceneItemBoundsMax(selectedTarget);
        const QVector3D groundCenter(center.x(), minPoint.y(), center.z());
        const QVector3D groundNW(minPoint.x(), minPoint.y(), minPoint.z());
        const QVector3D groundNE(maxPoint.x(), minPoint.y(), minPoint.z());
        const QVector3D groundSW(minPoint.x(), minPoint.y(), maxPoint.z());
        const QVector3D groundSE(maxPoint.x(), minPoint.y(), maxPoint.z());
        auto fmt = [](const QVector3D& v) -> QString
        {
            return QStringLiteral("(%1, %2, %3)")
                .arg(QString::number(v.x(), 'f', 3))
                .arg(QString::number(v.y(), 'f', 3))
                .arg(QString::number(v.z(), 'f', 3));
        };

        QString landmarksText;
        landmarksText += QStringLiteral("Center: %1\n").arg(fmt(center));
        landmarksText += QStringLiteral("Ground Center: %1\n").arg(fmt(groundCenter));
        landmarksText += QStringLiteral("Ground NW: %1\n").arg(fmt(groundNW));
        landmarksText += QStringLiteral("Ground NE: %1\n").arg(fmt(groundNE));
        landmarksText += QStringLiteral("Ground SW: %1\n").arg(fmt(groundSW));
        landmarksText += QStringLiteral("Ground SE: %1").arg(fmt(groundSE));
        m_placementLandmarksValue->setText(landmarksText);
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
        if (m_alignBottomToGroundButton) {
            m_alignBottomToGroundButton->setVisible(true);
            m_alignBottomToGroundButton->setEnabled(visible);
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
                                              const QString& physicsCoupling = QStringLiteral("AnimationOnly"),
                                              bool centroidNormalization = true,
                                              float trimStartNormalized = 0.0f,
                                              float trimEndNormalized = 1.0f,
                                              bool runtimeDriven = false)
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
        if (!visible || !m_animationClipCombo || !m_animationPlayingCheck || !m_animationLoopCheck ||
            !m_animationSpeedSpin || !m_animationCentroidNormalizeCheck || !m_animationTrimStartSpin ||
            !m_animationTrimEndSpin)
        {
            return;
        }
        if (m_animationClipSummaryValue)
        {
            QString summary;
            if (clips.isEmpty())
            {
                summary = QStringLiteral("No clips loaded.");
            }
            else
            {
                summary = QStringLiteral("Loaded clips (%1): %2")
                    .arg(clips.size())
                    .arg(clips.join(QStringLiteral(", ")));
            }
            if (runtimeDriven)
            {
                summary += QStringLiteral("\nRuntime locomotion uses the clip map below.");
            }
            m_animationClipSummaryValue->setText(summary);
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
        m_animationCentroidNormalizeCheck->blockSignals(true);
        m_animationCentroidNormalizeCheck->setChecked(centroidNormalization);
        m_animationCentroidNormalizeCheck->blockSignals(false);
        const float trimStart = std::clamp(trimStartNormalized, 0.0f, 1.0f);
        const float trimEnd = std::clamp(trimEndNormalized, 0.0f, 1.0f);
        m_animationTrimStartSpin->blockSignals(true);
        m_animationTrimEndSpin->blockSignals(true);
        m_animationTrimStartSpin->setValue(std::min(trimStart, trimEnd));
        m_animationTrimEndSpin->setValue(std::max(trimStart, trimEnd));
        m_animationTrimStartSpin->blockSignals(false);
        m_animationTrimEndSpin->blockSignals(false);
        m_animationPlayingCheck->setEnabled(!runtimeDriven);
        m_animationLoopCheck->setEnabled(!runtimeDriven);
        m_animationSpeedSpin->setEnabled(!runtimeDriven);
        m_animationTrimStartSpin->setEnabled(!runtimeDriven);
        m_animationTrimEndSpin->setEnabled(!runtimeDriven);
        
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
    const auto populateBindingCombo = [](QComboBox* combo, const QStringList& clips, const QString& selected)
    {
        if (!combo)
        {
            return;
        }
        combo->blockSignals(true);
        combo->clear();
        combo->addItem(QStringLiteral("Auto"), QString());
        for (const QString& clip : clips)
        {
            combo->addItem(clip, clip);
        }
        int index = selected.isEmpty() ? 0 : combo->findData(selected);
        if (index < 0)
        {
            index = 0;
        }
        combo->setCurrentIndex(index);
        combo->blockSignals(false);
    };
    const auto setCharacterAnimationBindingInspector = [this, populateBindingCombo](bool visible,
                                                                                     const QStringList& clips,
                                                                                     const ViewportHostWidget::SceneItem& item)
    {
        populateBindingCombo(m_characterIdleClipCombo, visible ? clips : QStringList{}, visible ? item.characterIdleClip : QString());
        populateBindingCombo(m_characterComeToRestClipCombo, visible ? clips : QStringList{}, visible ? item.characterComeToRestClip : QString());
        populateBindingCombo(m_characterWalkForwardClipCombo, visible ? clips : QStringList{}, visible ? item.characterWalkForwardClip : QString());
        populateBindingCombo(m_characterWalkBackwardClipCombo, visible ? clips : QStringList{}, visible ? item.characterWalkBackwardClip : QString());
        populateBindingCombo(m_characterWalkLeftClipCombo, visible ? clips : QStringList{}, visible ? item.characterWalkLeftClip : QString());
        populateBindingCombo(m_characterWalkRightClipCombo, visible ? clips : QStringList{}, visible ? item.characterWalkRightClip : QString());
        populateBindingCombo(m_characterRunClipCombo, visible ? clips : QStringList{}, visible ? item.characterRunClip : QString());
        populateBindingCombo(m_characterJumpClipCombo, visible ? clips : QStringList{}, visible ? item.characterJumpClip : QString());
        populateBindingCombo(m_characterFallClipCombo, visible ? clips : QStringList{}, visible ? item.characterFallClip : QString());
        populateBindingCombo(m_characterLandClipCombo, visible ? clips : QStringList{}, visible ? item.characterLandClip : QString());
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
    const auto setCharacterMotionInspector = [this](bool visible,
                                                    float turnResponsiveness = 10.0f,
                                                    float moveSpeed = 3.0f,
                                                    float idleAnimationSpeed = 1.0f,
                                                    float walkAnimationSpeed = 1.0f,
                                                    bool proceduralIdleEnabled = true)
    {
        if (!m_characterTurnResponsivenessSpin ||
            !m_characterMoveSpeedSpin ||
            !m_characterIdleAnimationSpeedSpin ||
            !m_characterWalkAnimationSpeedSpin ||
            !m_characterProceduralIdleCheck)
        {
            return;
        }
        m_characterTurnResponsivenessSpin->setVisible(true);
        m_characterTurnResponsivenessSpin->setEnabled(visible);
        m_characterMoveSpeedSpin->setVisible(true);
        m_characterMoveSpeedSpin->setEnabled(visible);
        m_characterIdleAnimationSpeedSpin->setVisible(true);
        m_characterIdleAnimationSpeedSpin->setEnabled(visible);
        m_characterWalkAnimationSpeedSpin->setVisible(true);
        m_characterWalkAnimationSpeedSpin->setEnabled(visible);
        m_characterProceduralIdleCheck->setVisible(true);
        m_characterProceduralIdleCheck->setEnabled(visible);
        if (visible)
        {
            m_characterTurnResponsivenessSpin->blockSignals(true);
            m_characterTurnResponsivenessSpin->setValue(turnResponsiveness);
            m_characterTurnResponsivenessSpin->blockSignals(false);
            m_characterMoveSpeedSpin->blockSignals(true);
            m_characterMoveSpeedSpin->setValue(moveSpeed);
            m_characterMoveSpeedSpin->blockSignals(false);
            m_characterIdleAnimationSpeedSpin->blockSignals(true);
            m_characterIdleAnimationSpeedSpin->setValue(idleAnimationSpeed);
            m_characterIdleAnimationSpeedSpin->blockSignals(false);
            m_characterWalkAnimationSpeedSpin->blockSignals(true);
            m_characterWalkAnimationSpeedSpin->setValue(walkAnimationSpeed);
            m_characterWalkAnimationSpeedSpin->blockSignals(false);
            m_characterProceduralIdleCheck->blockSignals(true);
            m_characterProceduralIdleCheck->setChecked(proceduralIdleEnabled);
            m_characterProceduralIdleCheck->blockSignals(false);
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
    const auto setTextInspector = [this](bool visible, const ViewportHostWidget::SceneItem* item = nullptr)
    {
        if (!m_textSection)
        {
            return;
        }
        m_textSection->setVisible(true);
        m_textSection->setEnabled(visible);
        if (!visible || !item)
        {
            return;
        }
        if (m_textContentEdit) m_textContentEdit->setText(item->textContent);
        if (m_textFontPathEdit) m_textFontPathEdit->setText(item->textFontPath);
        if (m_textPixelHeightSpin) m_textPixelHeightSpin->setValue(item->textPixelHeight);
        if (m_textBoldCheck) m_textBoldCheck->setChecked(item->textBold);
        if (m_textItalicCheck) m_textItalicCheck->setChecked(item->textItalic);
        if (m_textShadowCheck) m_textShadowCheck->setChecked(item->textShadow);
        if (m_textOutlineCheck) m_textOutlineCheck->setChecked(item->textOutline);
        if (m_textLetterSpacingSpin) m_textLetterSpacingSpin->setValue(item->textLetterSpacing);
        if (m_textExtrudeDepthSpin) m_textExtrudeDepthSpin->setValue(item->textExtrudeDepth);
        if (m_textExtrudeGlyphsOnlyCheck) m_textExtrudeGlyphsOnlyCheck->setChecked(item->textExtrudeGlyphsOnly);
        if (m_textDepthTestCheck) m_textDepthTestCheck->setChecked(item->textDepthTest);
        if (m_textDepthWriteCheck) m_textDepthWriteCheck->setChecked(item->textDepthWrite);
        if (m_textColorSwatch)
        {
            m_textColorSwatch->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid #888;").arg(item->textColor));
            m_textColorSwatch->setProperty("textColor", item->textColor);
        }
        if (m_textBgColorSwatch)
        {
            m_textBgColorSwatch->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid #888;").arg(item->textBackgroundColor));
            m_textBgColorSwatch->setProperty("textBgColor", item->textBackgroundColor);
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
        // Handle camera entries explicitly by node type. Negative hierarchy
        // rows are also used by the scene light, so row-based dispatch is not
        // sufficient here.
        if (nodeType == static_cast<int>(ViewportHostWidget::HierarchyNode::Type::Camera) && m_viewportHost)
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
            setBoundsSummary(-1);
            if (focusContextTab)
            {
                setElementDetailTab(3); // Camera
            }
            setPlacementInspector(-1);
            
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
                if (m_alignBottomToGroundButton) m_alignBottomToGroundButton->setEnabled(false);
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
                if (m_alignBottomToGroundButton) m_alignBottomToGroundButton->setEnabled(false);
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
            setTextInspector(false);
            setTexturePreview(QImage());
            configureElementInspectorForSelection(
                static_cast<int>(ViewportHostWidget::HierarchyNode::Type::Camera),
                row,
                meshIndex,
                primitiveIndex,
                false,
                false,
                focusContextTab);
            
            // Show follow target selector for all cameras
            setFollowTargetInspectorVisible(true, followTargetIndex);
            
            m_updatingInspector = false;
            return;
        }
        if (row == MainWindowShell::kHierarchyLightIndex)
        {
            const auto lightLabel = m_viewportHost
                ? detail::lightLabelFromSceneLight(m_viewportHost->sceneLight())
                : QStringLiteral("Directional Light");
            setValue(m_inspectorNameValue, lightLabel);
            setValue(m_inspectorPathValue, QStringLiteral("Scene Light"));
            setValue(m_animationModeValue, QStringLiteral("Static"));
            setValue(m_cameraTypeValue, QStringLiteral("-"));
            setBoundsSummary(-1);
            if (focusContextTab)
            {
                setElementDetailTab(1); // Visual
            }
            setPlacementInspector(-1);
            if (m_viewportHost)
            {
                const auto light = m_viewportHost->sceneLight();
                if (m_inspectorTranslationX) m_inspectorTranslationX->setValue(light.editorProxyPosition.x());
                if (m_inspectorTranslationY) m_inspectorTranslationY->setValue(light.editorProxyPosition.y());
                if (m_inspectorTranslationZ) m_inspectorTranslationZ->setValue(light.editorProxyPosition.z());
            }
            if (m_inspectorRotationX) m_inspectorRotationX->setValue(0.0);
            if (m_inspectorRotationY) m_inspectorRotationY->setValue(0.0);
            if (m_inspectorRotationZ) m_inspectorRotationZ->setValue(0.0);
            if (m_inspectorScaleX) m_inspectorScaleX->setValue(1.0);
            if (m_inspectorScaleY) m_inspectorScaleY->setValue(1.0);
            if (m_inspectorScaleZ) m_inspectorScaleZ->setValue(1.0);
            setLoadInspectorVisible(false);
            if (m_primitiveForceAlphaButton) m_primitiveForceAlphaButton->setChecked(false);
            if (m_paintOverrideCheck) m_paintOverrideCheck->setChecked(false);
            setTransformInspectorVisible(true);
            setPrimitiveInspectorVisible(false);
            setLightInspectorVisible(true);
            setFollowTargetInspectorVisible(false);
            setAnimationInspector(false);
            setGravityInspector(false);
            setCharacterMotionInspector(false);
            setObjectRuntimeInspector(false);
            setMotionOverlayInspector(true);
            setTextInspector(false);
            if (m_lockScaleXYZCheck) {
                m_lockScaleXYZCheck->setVisible(true);
                m_lockScaleXYZCheck->setEnabled(false);
            }
            if (m_inspectorTranslationX) m_inspectorTranslationX->setEnabled(true);
            if (m_inspectorTranslationY) m_inspectorTranslationY->setEnabled(true);
            if (m_inspectorTranslationZ) m_inspectorTranslationZ->setEnabled(true);
            if (m_inspectorRotationX) m_inspectorRotationX->setEnabled(false);
            if (m_inspectorRotationY) m_inspectorRotationY->setEnabled(false);
            if (m_inspectorRotationZ) m_inspectorRotationZ->setEnabled(false);
            if (m_inspectorScaleX) m_inspectorScaleX->setEnabled(false);
            if (m_inspectorScaleY) m_inspectorScaleY->setEnabled(false);
            if (m_inspectorScaleZ) m_inspectorScaleZ->setEnabled(false);
            if (m_alignBottomToGroundButton) {
                m_alignBottomToGroundButton->setVisible(true);
                m_alignBottomToGroundButton->setEnabled(false);
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
            configureElementInspectorForSelection(
                static_cast<int>(ViewportHostWidget::HierarchyNode::Type::Light),
                row,
                meshIndex,
                primitiveIndex,
                false,
                false,
                focusContextTab);
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
        setBoundsSummary(-1);
        setPlacementInspector(-1);
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
        setTextInspector(false);
        if (m_lockScaleXYZCheck) {
            m_lockScaleXYZCheck->setVisible(true);
            m_lockScaleXYZCheck->setEnabled(false);
        }
        if (m_alignBottomToGroundButton) {
            m_alignBottomToGroundButton->setVisible(true);
            m_alignBottomToGroundButton->setEnabled(false);
        }
        setTexturePreview(QImage());
        configureElementInspectorForSelection(
            nodeType,
            row,
            meshIndex,
            primitiveIndex,
            false,
            false,
            focusContextTab);
        m_updatingInspector = false;
        return;
    }

    const auto& item = m_sceneItems[row];
    if (focusContextTab)
    {
        setElementDetailTab(0); // Overview
    }
    setValue(m_inspectorNameValue, item.name);
    setValue(m_inspectorPathValue, QDir::toNativeSeparators(item.sourcePath));
    setValue(m_animationModeValue,
             m_viewportHost ? m_viewportHost->animationExecutionMode(row, meshIndex, primitiveIndex)
                            : QStringLiteral("-"));
    setBoundsSummary(row);
    setPlacementInspector(row);
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
    if (m_alignBottomToGroundButton) m_alignBottomToGroundButton->setEnabled(true);
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
    const bool isTextItem = item.sourcePath.startsWith(QStringLiteral("text://"), Qt::CaseInsensitive);
    const QString suffix = QFileInfo(item.sourcePath).suffix().toLower();
    const bool loadVisible = suffix == QStringLiteral("gltf") || suffix == QStringLiteral("glb");
    const bool primitiveControlsVisible = m_viewportHost && meshIndex >= 0 && primitiveIndex >= 0;
    const bool sceneCullControlsVisible = m_viewportHost &&
        nodeType == static_cast<int>(ViewportHostWidget::HierarchyNode::Type::SceneItem);
    setLoadInspectorVisible(!isTextItem && loadVisible, item.meshConsolidationEnabled);
    setPrimitiveInspectorVisible(
        !isTextItem && (primitiveControlsVisible || sceneCullControlsVisible),
        (!isTextItem && m_viewportHost)
            ? (primitiveControlsVisible ? m_viewportHost->primitiveCullMode(row, meshIndex, primitiveIndex)
                                        : m_viewportHost->sceneItemCullMode(row))
            : QStringLiteral("back"));
    if (m_primitiveForceAlphaButton && !primitiveControlsVisible)
    {
        m_primitiveForceAlphaButton->setEnabled(false);
    }
    if (m_materialSection)
    {
        m_materialSection->setVisible(true);
        m_materialSection->setEnabled(!isTextItem && (loadVisible || primitiveControlsVisible || sceneCullControlsVisible));
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
    setTextInspector(isTextItem, &item);
    if (isTextItem)
    {
        setFollowTargetInspectorVisible(false);
        setFollowParamsVisible(false);
        setAnimationInspector(false);
        setGravityInspector(false);
        setCharacterMotionInspector(false);
        setObjectRuntimeInspector(false);
        setMotionOverlayInspector(true);
        setTexturePreview(QImage());
        configureElementInspectorForSelection(
            nodeType,
            row,
            meshIndex,
            primitiveIndex,
            false,
            true,
            focusContextTab);
        m_updatingInspector = false;
        return;
    }
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
    const bool runtimeDrivenAnimation = m_viewportHost ? m_viewportHost->isCharacterControlEnabled(row) : false;
    setAnimationInspector(!isTextItem && !animationClips.isEmpty(),
                          animationClips,
                          selectedClip,
                          item.animationPlaying,
                          item.animationLoop,
                          item.animationSpeed,
                          item.animationPhysicsCoupling,
                          item.animationCentroidNormalization,
                          item.animationTrimStartNormalized,
                          item.animationTrimEndNormalized,
                          runtimeDrivenAnimation);
    setCharacterAnimationBindingInspector(!isTextItem && !animationClips.isEmpty(), animationClips, item);
    setGravityInspector(!isTextItem, item.useGravity, item.customGravity);
    setCharacterMotionInspector(!isTextItem,
                                item.characterTurnResponsiveness,
                                item.characterMoveSpeed,
                                item.characterIdleAnimationSpeed,
                                item.characterWalkAnimationSpeed,
                                item.characterProceduralIdleEnabled);
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
        setFollowParamsVisible(true);
        if (m_freeFlyCameraCheck)
        {
            m_freeFlyCameraCheck->setEnabled(true);
        }
        if (m_wasdRoutingCombo)
        {
            m_wasdRoutingCombo->setEnabled(true);
        }
        if (m_invertHorizontalDragCheck)
        {
            m_invertHorizontalDragCheck->setEnabled(true);
        }
        if (m_nearClipSpin)
        {
            m_nearClipSpin->setEnabled(true);
        }
        if (m_farClipSpin)
        {
            m_farClipSpin->setEnabled(true);
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
            const bool sprintRequested = runtime.value(QStringLiteral("keyShift")).toBool(false);
            const QVector3D inputDir = vec3FromJson(runtime.value(QStringLiteral("inputDir")));
            const QVector3D velocity = vec3FromJson(runtime.value(QStringLiteral("velocity")));
            const double effectiveMoveSpeed = runtime.value(QStringLiteral("characterEffectiveMoveSpeed"))
                                                  .toDouble(item.characterMoveSpeed);
            const QString currentAnimState = runtime.value(QStringLiteral("currentAnimState")).toString(QStringLiteral("Unknown"));
            const QString resolvedAnimState = runtime.value(QStringLiteral("resolvedAnimState")).toString(currentAnimState);
            const QString animationStateSource = runtime.value(QStringLiteral("animationStateSource")).toString(QStringLiteral("Unknown"));
            const double currentAnimWeight = runtime.value(QStringLiteral("currentAnimWeight")).toDouble(0.0);
            const double currentAnimSpeed = runtime.value(QStringLiteral("currentAnimSpeed")).toDouble(0.0);
            const QString runtimeActiveClip = runtime.value(QStringLiteral("runtimeActiveClip")).toString();
            const bool runtimeCentroidNormalization = runtime.value(QStringLiteral("runtimeAnimationCentroidNormalization"))
                                                          .toBool(item.animationCentroidNormalization);
            const double runtimeTrimStart = runtime.value(QStringLiteral("runtimeAnimationTrimStartNormalized"))
                                                .toDouble(item.animationTrimStartNormalized);
            const double runtimeTrimEnd = runtime.value(QStringLiteral("runtimeAnimationTrimEndNormalized"))
                                              .toDouble(item.animationTrimEndNormalized);
            runtimeClip = runtimeActiveClip.isEmpty() ? runtimeClip : runtimeActiveClip;
            runtimePlaying = runtime.value(QStringLiteral("runtimeAnimationPlaying")).toBool(runtimePlaying);
            runtimeLoop = runtime.value(QStringLiteral("runtimeAnimationLoop")).toBool(runtimeLoop);
            runtimeSpeed = runtime.value(QStringLiteral("runtimeAnimationSpeed")).toDouble(runtimeSpeed);

            kinematicInfo = QStringLiteral(
                                "Coupling: %1\n"
                                "Controllable: %2  Grounded: %3  JumpReq: %4  Sprint: %5\n"
                                "MoveSpeed: %6\n"
                                "InputDir: (%7, %8, %9)\n"
                                "Velocity: (%10, %11, %12)")
                                .arg(item.animationPhysicsCoupling.isEmpty()
                                         ? QStringLiteral("AnimationOnly")
                                         : item.animationPhysicsCoupling)
                                .arg(isControllable ? QStringLiteral("true") : QStringLiteral("false"))
                                .arg(isGrounded ? QStringLiteral("true") : QStringLiteral("false"))
                                .arg(jumpRequested ? QStringLiteral("true") : QStringLiteral("false"))
                                .arg(sprintRequested ? QStringLiteral("true") : QStringLiteral("false"))
                                .arg(QString::number(effectiveMoveSpeed, 'f', 2))
                                .arg(QString::number(inputDir.x(), 'f', 2))
                                .arg(QString::number(inputDir.y(), 'f', 2))
                                .arg(QString::number(inputDir.z(), 'f', 2))
                                .arg(QString::number(velocity.x(), 'f', 2))
                                .arg(QString::number(velocity.y(), 'f', 2))
                                .arg(QString::number(velocity.z(), 'f', 2));

            animationRuntimeInfo = QStringLiteral(
                                       "Resolved Clip: %1 | Playing: %2 | Loop: %3 | Speed: %4\n"
                                       "State Source: %5  State: %6  CharacterState: %7\n"
                                       "RuntimeSpeed: %8  BlendWeight: %9\n"
                                       "CentroidNormalization: %10  Trim: [%11, %12]")
                                       .arg(runtimeClip.isEmpty() ? QStringLiteral("-") : runtimeClip)
                                       .arg(runtimePlaying ? QStringLiteral("true") : QStringLiteral("false"))
                                       .arg(runtimeLoop ? QStringLiteral("true") : QStringLiteral("false"))
                                       .arg(QString::number(runtimeSpeed, 'f', 2))
                                       .arg(animationStateSource)
                                       .arg(resolvedAnimState)
                                       .arg(currentAnimState)
                                       .arg(QString::number(currentAnimSpeed, 'f', 2))
                                       .arg(QString::number(currentAnimWeight, 'f', 2))
                                       .arg(runtimeCentroidNormalization ? QStringLiteral("true") : QStringLiteral("false"))
                                       .arg(QString::number(runtimeTrimStart, 'f', 2))
                                       .arg(QString::number(runtimeTrimEnd, 'f', 2));
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
    configureElementInspectorForSelection(
        nodeType,
        row,
        meshIndex,
        primitiveIndex,
        !animationClips.isEmpty(),
        false,
        focusContextTab);
    m_updatingInspector = false;
}

}  // namespace motive::ui
