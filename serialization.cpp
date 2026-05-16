#include "shell.h"
#include "camera_follow_settings.h"
#include "viewport_internal_utils.h"

#include <QFileInfo>
#include <QJsonObject>

namespace motive::ui {

QJsonObject cameraConfigToJson(const ViewportHostWidget::CameraConfig& config)
{
    QJsonObject obj;
    obj[QStringLiteral("id")] = config.id;
    obj[QStringLiteral("name")] = config.name;
    obj[QStringLiteral("type")] = config.type == ViewportHostWidget::CameraConfig::Type::Free ? QStringLiteral("free") : QStringLiteral("follow");
    obj[QStringLiteral("position")] = QJsonArray{config.position.x(), config.position.y(), config.position.z()};
    obj[QStringLiteral("rotation")] = QJsonArray{config.rotation.x(), config.rotation.y(), config.rotation.z()};
    obj[QStringLiteral("followTargetIndex")] = config.followTargetIndex;
    obj[QStringLiteral("followDistance")] = config.followDistance;
    obj[QStringLiteral("followYaw")] = config.followYaw;
    obj[QStringLiteral("followPitch")] = config.followPitch;
    obj[QStringLiteral("followSmoothSpeed")] = config.followSmoothSpeed;
    obj[QStringLiteral("followTargetOffset")] = QJsonArray{config.followTargetOffset.x(), config.followTargetOffset.y(), config.followTargetOffset.z()};
    obj[QStringLiteral("freeFly")] = config.freeFly;
    obj[QStringLiteral("invertHorizontalDrag")] = config.invertHorizontalDrag;
    obj[QStringLiteral("nearClip")] = config.nearClip;
    obj[QStringLiteral("farClip")] = config.farClip;
    return obj;
}

ViewportHostWidget::CameraConfig cameraConfigFromJson(const QJsonObject& obj)
{
    ViewportHostWidget::CameraConfig config;
    
    auto readVector3D = [](const QJsonValue& value, const QVector3D& fallback)
    {
        const QJsonArray array = value.toArray();
        if (array.size() != 3)
            return fallback;
        return QVector3D(
            static_cast<float>(array.at(0).toDouble(fallback.x())),
            static_cast<float>(array.at(1).toDouble(fallback.y())),
            static_cast<float>(array.at(2).toDouble(fallback.z()))
        );
    };
    
    config.name = obj.value(QStringLiteral("name")).toString(QStringLiteral("Camera"));
    config.id = obj.value(QStringLiteral("id")).toString();
    QString typeStr = obj.value(QStringLiteral("type")).toString(QStringLiteral("free"));
    config.type = (typeStr == QStringLiteral("follow")) 
        ? ViewportHostWidget::CameraConfig::Type::Follow 
        : ViewportHostWidget::CameraConfig::Type::Free;
    
    config.position = readVector3D(obj.value(QStringLiteral("position")), QVector3D(0.0f, 0.0f, 3.0f));
    config.rotation = readVector3D(obj.value(QStringLiteral("rotation")), QVector3D(0.0f, 0.0f, 0.0f));
    config.followTargetIndex = obj.value(QStringLiteral("followTargetIndex")).toInt(-1);
    config.followDistance = static_cast<float>(obj.value(QStringLiteral("followDistance")).toDouble(5.0));
    config.followYaw = static_cast<float>(obj.value(QStringLiteral("followYaw")).toDouble(0.0));
    config.followPitch = static_cast<float>(obj.value(QStringLiteral("followPitch")).toDouble(20.0));
    config.followSmoothSpeed = static_cast<float>(
        obj.value(QStringLiteral("followSmoothSpeed")).toDouble(followcam::kDefaultSmoothSpeed));
    config.followTargetOffset = readVector3D(obj.value(QStringLiteral("followTargetOffset")), QVector3D(0.0f, 0.0f, 0.0f));
    config.freeFly = obj.value(QStringLiteral("freeFly")).toBool(true);
    if (obj.contains(QStringLiteral("invertHorizontalDrag")))
    {
        config.invertHorizontalDrag = obj.value(QStringLiteral("invertHorizontalDrag")).toBool(false);
    }
    else
    {
        config.invertHorizontalDrag = (config.name == QStringLiteral("Main Fly Camera"));
    }
    config.nearClip = static_cast<float>(obj.value(QStringLiteral("nearClip")).toDouble(0.1));
    config.farClip = static_cast<float>(obj.value(QStringLiteral("farClip")).toDouble(100.0));
    
    return config;
}

QJsonObject MainWindowShell::sceneLightToJson(const ViewportHostWidget::SceneLight& light)
{
    return QJsonObject{
        {QStringLiteral("type"), light.type},
        {QStringLiteral("exists"), light.exists},
        {QStringLiteral("translation"), QJsonArray{light.translation.x(), light.translation.y(), light.translation.z()}},
        {QStringLiteral("rotation"), QJsonArray{light.rotation.x(), light.rotation.y(), light.rotation.z()}},
        {QStringLiteral("scale"), QJsonArray{light.scale.x(), light.scale.y(), light.scale.z()}},
        {QStringLiteral("editorProxyPosition"), QJsonArray{light.editorProxyPosition.x(), light.editorProxyPosition.y(), light.editorProxyPosition.z()}},
        {QStringLiteral("color"), QJsonArray{light.color.x(), light.color.y(), light.color.z()}},
        {QStringLiteral("brightness"), light.brightness},
        {QStringLiteral("direction"), QJsonArray{light.direction.x(), light.direction.y(), light.direction.z()}},
        {QStringLiteral("ambient"), QJsonArray{light.ambient.x(), light.ambient.y(), light.ambient.z()}},
        {QStringLiteral("diffuse"), QJsonArray{light.diffuse.x(), light.diffuse.y(), light.diffuse.z()}}
    };
}

ViewportHostWidget::SceneLight MainWindowShell::sceneLightFromJson(const QJsonObject& object)
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
    const QJsonValue proxyPositionValue = object.contains(QStringLiteral("editorProxyPosition"))
        ? object.value(QStringLiteral("editorProxyPosition"))
        : object.value(QStringLiteral("position"));
    light.translation = readVector(object.value(QStringLiteral("translation")),
                                   readVector(proxyPositionValue, QVector3D(0.0f, 2.0f, 0.0f)));
    light.rotation = readVector(object.value(QStringLiteral("rotation")),
                                detail::lightRotationDegreesForDirection(
                                    readVector(object.value(QStringLiteral("direction")), QVector3D(0.0f, 0.0f, 1.0f))));
    light.scale = readVector(object.value(QStringLiteral("scale")), QVector3D(1.0f, 1.0f, 1.0f));
    light.editorProxyPosition = light.translation;
    light.color = readVector(object.value(QStringLiteral("color")), QVector3D(1.0f, 1.0f, 1.0f));
    light.brightness = static_cast<float>(object.value(QStringLiteral("brightness")).toDouble(1.0));
    const glm::vec3 direction = detail::lightDirectionFromRotationDegrees(light.rotation);
    light.direction = QVector3D(direction.x, direction.y, direction.z);
    light.ambient = readVector(object.value(QStringLiteral("ambient")), QVector3D(0.1f, 0.1f, 0.1f));
    light.diffuse = readVector(object.value(QStringLiteral("diffuse")), QVector3D(0.9f, 0.9f, 0.9f));
    return light;
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
            {QStringLiteral("animationCentroidNormalization"), item.animationCentroidNormalization},
            {QStringLiteral("animationTrimStartNormalized"), item.animationTrimStartNormalized},
            {QStringLiteral("animationTrimEndNormalized"), item.animationTrimEndNormalized},
            {QStringLiteral("visible"), item.visible},
            {QStringLiteral("animationPhysicsCoupling"), item.animationPhysicsCoupling},
            {QStringLiteral("useGravity"), item.useGravity},
            {QStringLiteral("customGravity"), QJsonArray{item.customGravity.x(), item.customGravity.y(), item.customGravity.z()}},
            {QStringLiteral("characterTurnResponsiveness"), item.characterTurnResponsiveness},
            {QStringLiteral("characterMoveSpeed"), item.characterMoveSpeed},
            {QStringLiteral("characterIdleAnimationSpeed"), item.characterIdleAnimationSpeed},
            {QStringLiteral("characterWalkAnimationSpeed"), item.characterWalkAnimationSpeed},
            {QStringLiteral("characterProceduralIdleEnabled"), item.characterProceduralIdleEnabled},
            {QStringLiteral("characterIdleClip"), item.characterIdleClip},
            {QStringLiteral("characterComeToRestClip"), item.characterComeToRestClip},
            {QStringLiteral("characterWalkForwardClip"), item.characterWalkForwardClip},
            {QStringLiteral("characterWalkBackwardClip"), item.characterWalkBackwardClip},
            {QStringLiteral("characterWalkLeftClip"), item.characterWalkLeftClip},
            {QStringLiteral("characterWalkRightClip"), item.characterWalkRightClip},
            {QStringLiteral("characterRunClip"), item.characterRunClip},
            {QStringLiteral("characterJumpClip"), item.characterJumpClip},
            {QStringLiteral("characterFallClip"), item.characterFallClip},
            {QStringLiteral("characterLandClip"), item.characterLandClip},
            {QStringLiteral("primitiveOverrides"), item.primitiveOverrides},
            {QStringLiteral("focusPointOffset"), QJsonArray{item.focusPointOffset.x(), item.focusPointOffset.y(), item.focusPointOffset.z()}},
            {QStringLiteral("focusDistance"), item.focusDistance},
            {QStringLiteral("focusCameraOffset"), QJsonArray{item.focusCameraOffset.x(), item.focusCameraOffset.y(), item.focusCameraOffset.z()}},
            {QStringLiteral("focusCameraOffsetValid"), item.focusCameraOffsetValid},
            {QStringLiteral("characterAiEnabled"), item.characterAiEnabled},
            {QStringLiteral("characterAiUseInverseColors"), item.characterAiUseInverseColors},
            {QStringLiteral("characterAiTargetSceneIndex"), item.characterAiTargetSceneIndex},
            {QStringLiteral("characterAiMoveSpeed"), item.characterAiMoveSpeed},
            {QStringLiteral("characterAiAttackDistance"), item.characterAiAttackDistance},
            {QStringLiteral("textContent"), item.textContent},
            {QStringLiteral("textFontPath"), item.textFontPath},
            {QStringLiteral("textPixelHeight"), item.textPixelHeight},
            {QStringLiteral("textBold"), item.textBold},
            {QStringLiteral("textItalic"), item.textItalic},
            {QStringLiteral("textShadow"), item.textShadow},
            {QStringLiteral("textOutline"), item.textOutline},
            {QStringLiteral("textLetterSpacing"), item.textLetterSpacing},
            {QStringLiteral("textColor"), item.textColor},
            {QStringLiteral("textBackgroundColor"), item.textBackgroundColor},
            {QStringLiteral("textExtrudeDepth"), item.textExtrudeDepth},
            {QStringLiteral("textExtrudeGlyphsOnly"), item.textExtrudeGlyphsOnly},
            {QStringLiteral("textDepthTest"), item.textDepthTest},
            {QStringLiteral("textDepthWrite"), item.textDepthWrite}
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
        ViewportHostWidget::SceneItem sceneItem{
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
            object.value(QStringLiteral("animationCentroidNormalization")).toBool(true),
            static_cast<float>(object.value(QStringLiteral("animationTrimStartNormalized")).toDouble(0.0)),
            static_cast<float>(object.value(QStringLiteral("animationTrimEndNormalized")).toDouble(1.0)),
            object.value(QStringLiteral("visible")).toBool(true),
            object.value(QStringLiteral("animationPhysicsCoupling")).toString(QStringLiteral("AnimationOnly")),
            object.value(QStringLiteral("useGravity")).toBool(true),
            readVector(object.value(QStringLiteral("customGravity")), QVector3D(0.0f, 0.0f, 0.0f)),
            static_cast<float>(object.value(QStringLiteral("characterTurnResponsiveness")).toDouble(10.0)),
            static_cast<float>(object.value(QStringLiteral("characterMoveSpeed")).toDouble(3.0)),
            static_cast<float>(object.value(QStringLiteral("characterIdleAnimationSpeed")).toDouble(1.0)),
            static_cast<float>(object.value(QStringLiteral("characterWalkAnimationSpeed")).toDouble(1.0)),
            object.value(QStringLiteral("characterProceduralIdleEnabled")).toBool(true),
            object.value(QStringLiteral("characterIdleClip")).toString(),
            object.value(QStringLiteral("characterComeToRestClip")).toString(),
            object.value(QStringLiteral("characterWalkForwardClip")).toString(),
            object.value(QStringLiteral("characterWalkBackwardClip")).toString(),
            object.value(QStringLiteral("characterWalkLeftClip")).toString(),
            object.value(QStringLiteral("characterWalkRightClip")).toString(),
            object.value(QStringLiteral("characterRunClip")).toString(),
            object.value(QStringLiteral("characterJumpClip")).toString(),
            object.value(QStringLiteral("characterFallClip")).toString(),
            object.value(QStringLiteral("characterLandClip")).toString(),
            object.value(QStringLiteral("primitiveOverrides")).toArray(),
            readVector(object.value(QStringLiteral("focusPointOffset")), QVector3D(0.0f, 0.0f, 0.0f)),
            static_cast<float>(object.value(QStringLiteral("focusDistance")).toDouble(0.0)),
            readVector(object.value(QStringLiteral("focusCameraOffset")), QVector3D(0.0f, 0.0f, 0.0f)),
            object.value(QStringLiteral("focusCameraOffsetValid")).toBool(false)
        };
        sceneItem.characterAiEnabled = object.value(QStringLiteral("characterAiEnabled")).toBool(false);
        sceneItem.characterAiUseInverseColors = object.value(QStringLiteral("characterAiUseInverseColors")).toBool(false);
        sceneItem.characterAiTargetSceneIndex = object.value(QStringLiteral("characterAiTargetSceneIndex")).toInt(-1);
        sceneItem.characterAiMoveSpeed = static_cast<float>(object.value(QStringLiteral("characterAiMoveSpeed")).toDouble(2.4));
        sceneItem.characterAiAttackDistance = static_cast<float>(object.value(QStringLiteral("characterAiAttackDistance")).toDouble(1.2));
        sceneItem.textContent = object.value(QStringLiteral("textContent")).toString(QStringLiteral("Text"));
        sceneItem.textFontPath = object.value(QStringLiteral("textFontPath")).toString();
        sceneItem.textPixelHeight = object.value(QStringLiteral("textPixelHeight")).toInt(56);
        sceneItem.textBold = object.value(QStringLiteral("textBold")).toBool(false);
        sceneItem.textItalic = object.value(QStringLiteral("textItalic")).toBool(false);
        sceneItem.textShadow = object.value(QStringLiteral("textShadow")).toBool(true);
        sceneItem.textOutline = object.value(QStringLiteral("textOutline")).toBool(false);
        sceneItem.textLetterSpacing = object.value(QStringLiteral("textLetterSpacing")).toInt(0);
        sceneItem.textColor = object.value(QStringLiteral("textColor")).toString(QStringLiteral("#ffffffff"));
        sceneItem.textBackgroundColor = object.value(QStringLiteral("textBackgroundColor")).toString(QStringLiteral("#aa000000"));
        sceneItem.textExtrudeDepth = static_cast<float>(object.value(QStringLiteral("textExtrudeDepth")).toDouble(0.02));
        sceneItem.textExtrudeGlyphsOnly = object.value(QStringLiteral("textExtrudeGlyphsOnly")).toBool(true);
        sceneItem.textDepthTest = object.value(QStringLiteral("textDepthTest")).toBool(false);
        sceneItem.textDepthWrite = object.value(QStringLiteral("textDepthWrite")).toBool(false);
        result.push_back(sceneItem);
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

QJsonArray MainWindowShell::cameraConfigsToJson(const QList<ViewportHostWidget::CameraConfig>& configs) const
{
    QJsonArray array;
    for (const auto& config : configs)
    {
        array.push_back(cameraConfigToJson(config));
    }
    return array;
}

QList<ViewportHostWidget::CameraConfig> MainWindowShell::cameraConfigsFromJson(const QJsonArray& configs) const
{
    QList<ViewportHostWidget::CameraConfig> result;
    for (const QJsonValue& value : configs)
    {
        result.push_back(cameraConfigFromJson(value.toObject()));
    }
    return result;
}

}  // namespace motive::ui
