#include "control_command_service.h"

#include "host_widget.h"
#include "shell.h"

#include <QJsonArray>
#include <QVector3D>

namespace motive::ui {

bool ControlCommandService::handlePrimitive(const QJsonObject& body, QJsonObject& result) const
{
    auto* viewport = m_window.viewportHost();
    if (!viewport)
    {
        result.insert(QStringLiteral("error"), QStringLiteral("viewport unavailable"));
        return false;
    }

    const int sceneIndex = body.value(QStringLiteral("sceneIndex")).toInt(-1);
    const int meshIndex = body.value(QStringLiteral("meshIndex")).toInt(-1);
    const int primitiveIndex = body.value(QStringLiteral("primitiveIndex")).toInt(-1);
    const QString cullMode = body.value(QStringLiteral("cullMode")).toString();
    if (sceneIndex < 0 || meshIndex < 0 || primitiveIndex < 0 || cullMode.isEmpty())
    {
        result.insert(QStringLiteral("error"), QStringLiteral("sceneIndex, meshIndex, primitiveIndex, and cullMode are required"));
        return false;
    }
    viewport->setPrimitiveCullMode(sceneIndex, meshIndex, primitiveIndex, cullMode);
    result.insert(QStringLiteral("sceneIndex"), sceneIndex);
    result.insert(QStringLiteral("meshIndex"), meshIndex);
    result.insert(QStringLiteral("primitiveIndex"), primitiveIndex);
    result.insert(QStringLiteral("cullMode"), cullMode);
    return true;
}

bool ControlCommandService::handleSceneItem(const QJsonObject& body, QJsonObject& result) const
{
    auto* viewport = m_window.viewportHost();
    if (!viewport)
    {
        result.insert(QStringLiteral("error"), QStringLiteral("viewport unavailable"));
        return false;
    }

    const int sceneIndex = body.value(QStringLiteral("sceneIndex")).toInt(-1);
    if (sceneIndex < 0)
    {
        result.insert(QStringLiteral("error"), QStringLiteral("sceneIndex is required"));
        return false;
    }
    const auto items = viewport->sceneItems();
    if (sceneIndex >= items.size())
    {
        result.insert(QStringLiteral("error"), QStringLiteral("sceneIndex out of range"));
        return false;
    }
    if (body.contains(QStringLiteral("visible")))
    {
        viewport->setSceneItemVisible(sceneIndex, body.value(QStringLiteral("visible")).toBool(true));
    }
    if (body.value(QStringLiteral("setFocusPointFromCamera")).toBool(false))
    {
        viewport->captureSceneItemFocusFromCurrentCamera(sceneIndex);
    }
    if (body.contains(QStringLiteral("focusPointOffset")) ||
        body.contains(QStringLiteral("focusOffsetX")) ||
        body.contains(QStringLiteral("focusOffsetY")) ||
        body.contains(QStringLiteral("focusOffsetZ")) ||
        body.contains(QStringLiteral("focusDistance")))
    {
        QVector3D focusOffset = items[sceneIndex].focusPointOffset;
        float focusDistance = items[sceneIndex].focusDistance;

        if (body.contains(QStringLiteral("focusPointOffset")))
        {
            const QJsonArray offset = body.value(QStringLiteral("focusPointOffset")).toArray();
            if (offset.size() == 3)
            {
                focusOffset = QVector3D(
                    static_cast<float>(offset.at(0).toDouble(focusOffset.x())),
                    static_cast<float>(offset.at(1).toDouble(focusOffset.y())),
                    static_cast<float>(offset.at(2).toDouble(focusOffset.z())));
            }
        }
        if (body.contains(QStringLiteral("focusOffsetX")))
        {
            focusOffset.setX(static_cast<float>(body.value(QStringLiteral("focusOffsetX")).toDouble(focusOffset.x())));
        }
        if (body.contains(QStringLiteral("focusOffsetY")))
        {
            focusOffset.setY(static_cast<float>(body.value(QStringLiteral("focusOffsetY")).toDouble(focusOffset.y())));
        }
        if (body.contains(QStringLiteral("focusOffsetZ")))
        {
            focusOffset.setZ(static_cast<float>(body.value(QStringLiteral("focusOffsetZ")).toDouble(focusOffset.z())));
        }
        if (body.contains(QStringLiteral("focusDistance")))
        {
            focusDistance = static_cast<float>(body.value(QStringLiteral("focusDistance")).toDouble(focusDistance));
        }

        viewport->updateSceneItemFocusSettings(sceneIndex, focusOffset, focusDistance);
    }
    if (body.value(QStringLiteral("focus")).toBool(false))
    {
        viewport->focusSceneItem(sceneIndex);
    }
    result.insert(QStringLiteral("sceneIndex"), sceneIndex);
    const auto refreshedItems = viewport->sceneItems();
    if (sceneIndex >= 0 && sceneIndex < refreshedItems.size())
    {
        const auto& item = refreshedItems[sceneIndex];
        result.insert(QStringLiteral("focusPointOffset"), QJsonArray{item.focusPointOffset.x(), item.focusPointOffset.y(), item.focusPointOffset.z()});
        result.insert(QStringLiteral("focusDistance"), item.focusDistance);
        result.insert(QStringLiteral("focusCameraOffset"), QJsonArray{item.focusCameraOffset.x(), item.focusCameraOffset.y(), item.focusCameraOffset.z()});
        result.insert(QStringLiteral("focusCameraOffsetValid"), item.focusCameraOffsetValid);
    }
    return true;
}

bool ControlCommandService::handleAnimation(const QJsonObject& body, QJsonObject& result) const
{
    auto* viewport = m_window.viewportHost();
    if (!viewport)
    {
        result.insert(QStringLiteral("error"), QStringLiteral("viewport unavailable"));
        return false;
    }

    const int sceneIndex = body.value(QStringLiteral("sceneIndex")).toInt(-1);
    const auto items = viewport->sceneItems();
    if (sceneIndex < 0 || sceneIndex >= items.size())
    {
        result.insert(QStringLiteral("error"), QStringLiteral("valid sceneIndex is required"));
        return false;
    }

    QString clip = items[sceneIndex].activeAnimationClip;
    bool playing = items[sceneIndex].animationPlaying;
    bool loop = items[sceneIndex].animationLoop;
    float speed = items[sceneIndex].animationSpeed;

    if (body.contains(QStringLiteral("clip")))
    {
        clip = body.value(QStringLiteral("clip")).toString(clip);
    }
    if (body.contains(QStringLiteral("playing")))
    {
        playing = body.value(QStringLiteral("playing")).toBool(playing);
    }
    if (body.contains(QStringLiteral("loop")))
    {
        loop = body.value(QStringLiteral("loop")).toBool(loop);
    }
    if (body.contains(QStringLiteral("speed")))
    {
        speed = static_cast<float>(body.value(QStringLiteral("speed")).toDouble(speed));
    }

    viewport->updateSceneItemAnimationState(sceneIndex, clip, playing, loop, speed);

    if (body.value(QStringLiteral("select")).toBool(false))
    {
        m_window.selectHierarchySceneItem(sceneIndex);
    }

    const QJsonArray profile = viewport->sceneProfileJson();
    if (sceneIndex >= 0 && sceneIndex < profile.size() && profile.at(sceneIndex).isObject())
    {
        result.insert(QStringLiteral("scene"), profile.at(sceneIndex).toObject());
    }
    result.insert(QStringLiteral("sceneIndex"), sceneIndex);
    result.insert(QStringLiteral("clip"), clip);
    result.insert(QStringLiteral("playing"), playing);
    result.insert(QStringLiteral("loop"), loop);
    result.insert(QStringLiteral("speed"), speed);
    return true;
}

bool ControlCommandService::handleCharacter(const QJsonObject& body, QJsonObject& result) const
{
    auto* viewport = m_window.viewportHost();
    if (!viewport)
    {
        result.insert(QStringLiteral("error"), QStringLiteral("viewport unavailable"));
        return false;
    }

    int sceneIndex = body.value(QStringLiteral("sceneIndex")).toInt(-1);
    const QString name = body.value(QStringLiteral("name")).toString();

    if (!name.isEmpty())
    {
        const auto items = viewport->sceneItems();
        for (int i = 0; i < items.size(); ++i)
        {
            if (items[i].name == name)
            {
                sceneIndex = i;
                break;
            }
        }
    }

    if (sceneIndex < 0 && body.isEmpty())
    {
        QJsonArray characters;
        const auto items = viewport->sceneItems();
        for (int i = 0; i < items.size(); ++i)
        {
            QJsonObject charInfo;
            charInfo.insert(QStringLiteral("sceneIndex"), i);
            charInfo.insert(QStringLiteral("name"), items[i].name);
            charInfo.insert(QStringLiteral("isControllable"), viewport->isCharacterControlEnabled(i));
            characters.append(charInfo);
        }
        result.insert(QStringLiteral("characters"), characters);
        return true;
    }

    if (sceneIndex < 0)
    {
        result.insert(QStringLiteral("error"), QStringLiteral("sceneIndex or valid name is required"));
        return false;
    }

    if (body.contains(QStringLiteral("controllable")))
    {
        const bool enabled = body.value(QStringLiteral("controllable")).toBool(false);
        viewport->enableCharacterControl(sceneIndex, enabled);
        result.insert(QStringLiteral("controllable"), enabled);
    }

    if (body.contains(QStringLiteral("pattern")))
    {
        const QString pattern = body.value(QStringLiteral("pattern")).toString();
        const int stepDurationMs = body.value(QStringLiteral("stepDurationMs")).toInt(120);
        const int steps = body.value(QStringLiteral("steps")).toInt(32);
        const bool includeJump = body.value(QStringLiteral("includeJump")).toBool(false);
        const bool accepted = viewport->playCharacterInputPattern(sceneIndex,
                                                                  pattern,
                                                                  stepDurationMs,
                                                                  steps,
                                                                  includeJump);
        result.insert(QStringLiteral("pattern"), pattern);
        result.insert(QStringLiteral("patternAccepted"), accepted);
        result.insert(QStringLiteral("stepDurationMs"), stepDurationMs);
        result.insert(QStringLiteral("steps"), steps);
        result.insert(QStringLiteral("includeJump"), includeJump);
        if (!accepted)
        {
            result.insert(QStringLiteral("error"),
                          QStringLiteral("unknown pattern; valid values: circle, figure8, strafe"));
            return false;
        }
    }

    const bool hasInputPayload =
        body.contains(QStringLiteral("keyW")) ||
        body.contains(QStringLiteral("keyA")) ||
        body.contains(QStringLiteral("keyS")) ||
        body.contains(QStringLiteral("keyD")) ||
        body.contains(QStringLiteral("jump")) ||
        body.contains(QStringLiteral("durationMs")) ||
        body.contains(QStringLiteral("move"));
    if (hasInputPayload)
    {
        bool keyW = body.value(QStringLiteral("keyW")).toBool(false);
        bool keyA = body.value(QStringLiteral("keyA")).toBool(false);
        bool keyS = body.value(QStringLiteral("keyS")).toBool(false);
        bool keyD = body.value(QStringLiteral("keyD")).toBool(false);
        const bool jump = body.value(QStringLiteral("jump")).toBool(false);
        const int durationMs = body.value(QStringLiteral("durationMs")).toInt(250);

        if (body.contains(QStringLiteral("move")))
        {
            const QString move = body.value(QStringLiteral("move")).toString().toUpper();
            keyW = move.contains(QStringLiteral("W"));
            keyA = move.contains(QStringLiteral("A"));
            keyS = move.contains(QStringLiteral("S"));
            keyD = move.contains(QStringLiteral("D"));
        }

        const bool injected = viewport->injectCharacterInput(sceneIndex, keyW, keyA, keyS, keyD, jump, durationMs);
        result.insert(QStringLiteral("inputInjected"), injected);
        result.insert(QStringLiteral("keyW"), keyW);
        result.insert(QStringLiteral("keyA"), keyA);
        result.insert(QStringLiteral("keyS"), keyS);
        result.insert(QStringLiteral("keyD"), keyD);
        result.insert(QStringLiteral("jump"), jump);
        result.insert(QStringLiteral("durationMs"), durationMs);
    }

    result.insert(QStringLiteral("sceneIndex"), sceneIndex);
    result.insert(QStringLiteral("isControllable"), viewport->isCharacterControlEnabled(sceneIndex));
    return true;
}

bool ControlCommandService::handleRebuild(QJsonObject& result) const
{
    auto* viewport = m_window.viewportHost();
    if (!viewport)
    {
        result.insert(QStringLiteral("error"), QStringLiteral("viewport unavailable"));
        return false;
    }

    viewport->refresh();
    result.insert(QStringLiteral("ok"), true);
    result.insert(QStringLiteral("message"), QStringLiteral("Scene rebuilt"));
    return true;
}

bool ControlCommandService::handleRebuildCommand(const QJsonObject& body, QJsonObject& result) const
{
    Q_UNUSED(body);
    return handleRebuild(result);
}

bool ControlCommandService::handleReset(const QJsonObject& body, QJsonObject& result) const
{
    auto* viewport = m_window.viewportHost();
    if (!viewport)
    {
        result.insert(QStringLiteral("error"), QStringLiteral("viewport unavailable"));
        return false;
    }

    const QString resetType = body.value(QStringLiteral("type")).toString(QStringLiteral("cameras"));
    if (resetType == QStringLiteral("cameras"))
    {
        auto configs = viewport->cameraConfigs();
        while (configs.size() > 1)
        {
            viewport->deleteCamera(configs.size() - 1);
            configs = viewport->cameraConfigs();
        }

        if (configs.size() == 1)
        {
            ViewportHostWidget::CameraConfig defaultConfig;
            defaultConfig.name = QStringLiteral("Camera");
            defaultConfig.type = ViewportHostWidget::CameraConfig::Type::Free;
            defaultConfig.position = QVector3D(0.0f, 0.0f, 3.0f);
            defaultConfig.rotation = QVector3D(0.0f, 0.0f, 0.0f);
            defaultConfig.invertHorizontalDrag = true;
            viewport->updateCameraConfig(0, defaultConfig);
            viewport->setActiveCamera(0);
        }

        result.insert(QStringLiteral("ok"), true);
        result.insert(QStringLiteral("type"), QStringLiteral("cameras"));
        result.insert(QStringLiteral("message"), QStringLiteral("Cameras reset to default"));
        return true;
    }

    if (resetType == QStringLiteral("all"))
    {
        auto configs = viewport->cameraConfigs();
        while (configs.size() > 1)
        {
            viewport->deleteCamera(configs.size() - 1);
            configs = viewport->cameraConfigs();
        }
        if (configs.size() == 1)
        {
            ViewportHostWidget::CameraConfig defaultConfig;
            defaultConfig.name = QStringLiteral("Camera");
            defaultConfig.type = ViewportHostWidget::CameraConfig::Type::Free;
            defaultConfig.position = QVector3D(0.0f, 0.0f, 3.0f);
            defaultConfig.rotation = QVector3D(0.0f, 0.0f, 0.0f);
            defaultConfig.invertHorizontalDrag = true;
            viewport->updateCameraConfig(0, defaultConfig);
            viewport->setActiveCamera(0);
        }

        viewport->loadSceneFromItems(QList<ViewportHostWidget::SceneItem>{});

        result.insert(QStringLiteral("ok"), true);
        result.insert(QStringLiteral("type"), QStringLiteral("all"));
        result.insert(QStringLiteral("message"), QStringLiteral("Scene and cameras reset to default"));
        return true;
    }

    result.insert(QStringLiteral("error"), QStringLiteral("Unknown reset type. Use 'cameras' or 'all'"));
    return false;
}

}  // namespace motive::ui
