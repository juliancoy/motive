#include "main_window_shell.h"

#include <QFileInfo>
#include <QJsonObject>

namespace motive::ui {

QJsonObject MainWindowShell::sceneLightToJson(const ViewportHostWidget::SceneLight& light)
{
    return QJsonObject{
        {QStringLiteral("type"), light.type},
        {QStringLiteral("exists"), light.exists},
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
    light.color = readVector(object.value(QStringLiteral("color")), QVector3D(1.0f, 1.0f, 1.0f));
    light.brightness = static_cast<float>(object.value(QStringLiteral("brightness")).toDouble(1.0));
    light.direction = readVector(object.value(QStringLiteral("direction")), QVector3D(0.0f, 0.0f, 1.0f));
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
            {QStringLiteral("visible"), item.visible}
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
        result.push_back(ViewportHostWidget::SceneItem{
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
            object.value(QStringLiteral("visible")).toBool(true)
        });
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

}  // namespace motive::ui
