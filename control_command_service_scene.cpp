#include "control_command_service.h"

#include "host_widget.h"
#include "shell.h"

#include <QJsonArray>
#include <QVector3D>

namespace motive::ui {
namespace {

struct AnimationMutation
{
    QString clip;
    bool playing = true;
    bool loop = true;
    float speed = 1.0f;
    bool centroidNormalization = true;
    float trimStartNormalized = 0.0f;
    float trimEndNormalized = 1.0f;
    bool hasPlaybackMutation = false;
    bool hasProcessingMutation = false;
};

AnimationMutation parseAnimationMutation(const QJsonObject& body,
                                         const ViewportHostWidget::SceneItem& current)
{
    AnimationMutation mutation;
    mutation.clip = current.activeAnimationClip;
    mutation.playing = current.animationPlaying;
    mutation.loop = current.animationLoop;
    mutation.speed = current.animationSpeed;
    mutation.centroidNormalization = current.animationCentroidNormalization;
    mutation.trimStartNormalized = current.animationTrimStartNormalized;
    mutation.trimEndNormalized = current.animationTrimEndNormalized;

    if (body.contains(QStringLiteral("activeAnimationClip")))
    {
        mutation.clip = body.value(QStringLiteral("activeAnimationClip")).toString(mutation.clip);
        mutation.hasPlaybackMutation = true;
    }
    else if (body.contains(QStringLiteral("clip")))
    {
        mutation.clip = body.value(QStringLiteral("clip")).toString(mutation.clip);
        mutation.hasPlaybackMutation = true;
    }

    if (body.contains(QStringLiteral("animationPlaying")))
    {
        mutation.playing = body.value(QStringLiteral("animationPlaying")).toBool(mutation.playing);
        mutation.hasPlaybackMutation = true;
    }
    else if (body.contains(QStringLiteral("playing")))
    {
        mutation.playing = body.value(QStringLiteral("playing")).toBool(mutation.playing);
        mutation.hasPlaybackMutation = true;
    }

    if (body.contains(QStringLiteral("animationLoop")))
    {
        mutation.loop = body.value(QStringLiteral("animationLoop")).toBool(mutation.loop);
        mutation.hasPlaybackMutation = true;
    }
    else if (body.contains(QStringLiteral("loop")))
    {
        mutation.loop = body.value(QStringLiteral("loop")).toBool(mutation.loop);
        mutation.hasPlaybackMutation = true;
    }

    if (body.contains(QStringLiteral("animationSpeed")))
    {
        mutation.speed = static_cast<float>(body.value(QStringLiteral("animationSpeed")).toDouble(mutation.speed));
        mutation.hasPlaybackMutation = true;
    }
    else if (body.contains(QStringLiteral("speed")))
    {
        mutation.speed = static_cast<float>(body.value(QStringLiteral("speed")).toDouble(mutation.speed));
        mutation.hasPlaybackMutation = true;
    }

    if (body.contains(QStringLiteral("animationCentroidNormalization")))
    {
        mutation.centroidNormalization =
            body.value(QStringLiteral("animationCentroidNormalization")).toBool(mutation.centroidNormalization);
        mutation.hasProcessingMutation = true;
    }
    else if (body.contains(QStringLiteral("centroidNormalization")))
    {
        mutation.centroidNormalization =
            body.value(QStringLiteral("centroidNormalization")).toBool(mutation.centroidNormalization);
        mutation.hasProcessingMutation = true;
    }

    if (body.contains(QStringLiteral("animationTrimStartNormalized")))
    {
        mutation.trimStartNormalized = static_cast<float>(
            body.value(QStringLiteral("animationTrimStartNormalized")).toDouble(mutation.trimStartNormalized));
        mutation.hasProcessingMutation = true;
    }
    else if (body.contains(QStringLiteral("trimStartNormalized")))
    {
        mutation.trimStartNormalized = static_cast<float>(
            body.value(QStringLiteral("trimStartNormalized")).toDouble(mutation.trimStartNormalized));
        mutation.hasProcessingMutation = true;
    }

    if (body.contains(QStringLiteral("animationTrimEndNormalized")))
    {
        mutation.trimEndNormalized = static_cast<float>(
            body.value(QStringLiteral("animationTrimEndNormalized")).toDouble(mutation.trimEndNormalized));
        mutation.hasProcessingMutation = true;
    }
    else if (body.contains(QStringLiteral("trimEndNormalized")))
    {
        mutation.trimEndNormalized = static_cast<float>(
            body.value(QStringLiteral("trimEndNormalized")).toDouble(mutation.trimEndNormalized));
        mutation.hasProcessingMutation = true;
    }

    return mutation;
}

void applyAnimationMutation(ViewportHostWidget* viewport,
                            int sceneIndex,
                            const AnimationMutation& mutation,
                            bool forceApply)
{
    if (!viewport)
    {
        return;
    }

    if (forceApply || mutation.hasPlaybackMutation)
    {
        viewport->updateSceneItemAnimationState(
            sceneIndex, mutation.clip, mutation.playing, mutation.loop, mutation.speed);
    }
    if (forceApply || mutation.hasProcessingMutation)
    {
        viewport->updateSceneItemAnimationProcessing(sceneIndex,
                                                     mutation.centroidNormalization,
                                                     mutation.trimStartNormalized,
                                                     mutation.trimEndNormalized);
    }
}

}  // namespace

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
    if (sceneIndex < 0 || cullMode.isEmpty())
    {
        result.insert(QStringLiteral("error"), QStringLiteral("sceneIndex and cullMode are required"));
        return false;
    }
    if (meshIndex >= 0 && primitiveIndex >= 0)
    {
        viewport->setPrimitiveCullMode(sceneIndex, meshIndex, primitiveIndex, cullMode);
    }
    else
    {
        viewport->setSceneItemCullMode(sceneIndex, cullMode);
    }
    result.insert(QStringLiteral("sceneIndex"), sceneIndex);
    result.insert(QStringLiteral("meshIndex"), meshIndex);
    result.insert(QStringLiteral("primitiveIndex"), primitiveIndex);
    result.insert(QStringLiteral("cullMode"), cullMode);
    return true;
}

bool ControlCommandService::handlePlaneIndicators(const QJsonObject& body, QJsonObject& result) const
{
    auto* viewport = m_window.viewportHost();
    if (!viewport)
    {
        result.insert(QStringLiteral("error"), QStringLiteral("viewport unavailable"));
        return false;
    }

    const bool enabled = body.contains(QStringLiteral("enabled"))
        ? body.value(QStringLiteral("enabled")).toBool(false)
        : !viewport->coordinatePlaneIndicatorsEnabled();
    viewport->setCoordinatePlaneIndicatorsEnabled(enabled);
    result.insert(QStringLiteral("enabled"), viewport->coordinatePlaneIndicatorsEnabled());
    result.insert(QStringLiteral("sourcePaths"), QJsonArray{
        QStringLiteral("planes://xy"),
        QStringLiteral("planes://xz"),
        QStringLiteral("planes://yz"),
    });
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

    if (body.contains(QStringLiteral("translation")) ||
        body.contains(QStringLiteral("translationX")) ||
        body.contains(QStringLiteral("translationY")) ||
        body.contains(QStringLiteral("translationZ")) ||
        body.contains(QStringLiteral("rotation")) ||
        body.contains(QStringLiteral("rotationX")) ||
        body.contains(QStringLiteral("rotationY")) ||
        body.contains(QStringLiteral("rotationZ")) ||
        body.contains(QStringLiteral("scale")) ||
        body.contains(QStringLiteral("scaleX")) ||
        body.contains(QStringLiteral("scaleY")) ||
        body.contains(QStringLiteral("scaleZ")))
    {
        QVector3D translation = items[sceneIndex].translation;
        QVector3D rotation = items[sceneIndex].rotation;
        QVector3D scale = items[sceneIndex].scale;

        if (body.contains(QStringLiteral("translation")))
        {
            const QJsonArray value = body.value(QStringLiteral("translation")).toArray();
            if (value.size() == 3)
            {
                translation = QVector3D(
                    static_cast<float>(value.at(0).toDouble(translation.x())),
                    static_cast<float>(value.at(1).toDouble(translation.y())),
                    static_cast<float>(value.at(2).toDouble(translation.z())));
            }
        }
        if (body.contains(QStringLiteral("rotation")))
        {
            const QJsonArray value = body.value(QStringLiteral("rotation")).toArray();
            if (value.size() == 3)
            {
                rotation = QVector3D(
                    static_cast<float>(value.at(0).toDouble(rotation.x())),
                    static_cast<float>(value.at(1).toDouble(rotation.y())),
                    static_cast<float>(value.at(2).toDouble(rotation.z())));
            }
        }
        if (body.contains(QStringLiteral("scale")))
        {
            const QJsonArray value = body.value(QStringLiteral("scale")).toArray();
            if (value.size() == 3)
            {
                scale = QVector3D(
                    static_cast<float>(value.at(0).toDouble(scale.x())),
                    static_cast<float>(value.at(1).toDouble(scale.y())),
                    static_cast<float>(value.at(2).toDouble(scale.z())));
            }
        }

        if (body.contains(QStringLiteral("translationX")))
        {
            translation.setX(static_cast<float>(body.value(QStringLiteral("translationX")).toDouble(translation.x())));
        }
        if (body.contains(QStringLiteral("translationY")))
        {
            translation.setY(static_cast<float>(body.value(QStringLiteral("translationY")).toDouble(translation.y())));
        }
        if (body.contains(QStringLiteral("translationZ")))
        {
            translation.setZ(static_cast<float>(body.value(QStringLiteral("translationZ")).toDouble(translation.z())));
        }
        if (body.contains(QStringLiteral("rotationX")))
        {
            rotation.setX(static_cast<float>(body.value(QStringLiteral("rotationX")).toDouble(rotation.x())));
        }
        if (body.contains(QStringLiteral("rotationY")))
        {
            rotation.setY(static_cast<float>(body.value(QStringLiteral("rotationY")).toDouble(rotation.y())));
        }
        if (body.contains(QStringLiteral("rotationZ")))
        {
            rotation.setZ(static_cast<float>(body.value(QStringLiteral("rotationZ")).toDouble(rotation.z())));
        }
        if (body.contains(QStringLiteral("scaleX")))
        {
            scale.setX(static_cast<float>(body.value(QStringLiteral("scaleX")).toDouble(scale.x())));
        }
        if (body.contains(QStringLiteral("scaleY")))
        {
            scale.setY(static_cast<float>(body.value(QStringLiteral("scaleY")).toDouble(scale.y())));
        }
        if (body.contains(QStringLiteral("scaleZ")))
        {
            scale.setZ(static_cast<float>(body.value(QStringLiteral("scaleZ")).toDouble(scale.z())));
        }

        viewport->updateSceneItemTransform(sceneIndex, translation, rotation, scale);
    }

    if (body.contains(QStringLiteral("visible")))
    {
        viewport->setSceneItemVisible(sceneIndex, body.value(QStringLiteral("visible")).toBool(true));
    }
    if (body.contains(QStringLiteral("controllable")))
    {
        viewport->enableCharacterControl(sceneIndex, body.value(QStringLiteral("controllable")).toBool(false));
    }
    if (body.contains(QStringLiteral("freeFly")))
    {
        viewport->setFreeFlyCameraEnabled(body.value(QStringLiteral("freeFly")).toBool(true));
    }
    if (body.contains(QStringLiteral("meshConsolidationEnabled")))
    {
        viewport->setSceneItemMeshConsolidationEnabled(
            sceneIndex,
            body.value(QStringLiteral("meshConsolidationEnabled")).toBool(items[sceneIndex].meshConsolidationEnabled));
    }
    if (body.contains(QStringLiteral("cullMode")))
    {
        viewport->setSceneItemCullMode(sceneIndex, body.value(QStringLiteral("cullMode")).toString(QStringLiteral("back")));
    }
    if (body.value(QStringLiteral("alignBottomToGround")).toBool(false))
    {
        const float groundY = static_cast<float>(body.value(QStringLiteral("groundY")).toDouble(0.0));
        if (!viewport->alignSceneItemBottomToGround(sceneIndex, groundY))
        {
            result.insert(QStringLiteral("error"), QStringLiteral("failed to align scene item bottom to ground"));
            return false;
        }
        result.insert(QStringLiteral("alignedBottomToGround"), true);
        result.insert(QStringLiteral("groundY"), groundY);
    }
    const AnimationMutation animationMutation = parseAnimationMutation(body, items[sceneIndex]);
    applyAnimationMutation(viewport, sceneIndex, animationMutation, false);
    if (body.contains(QStringLiteral("characterRestPointOnReleaseEnabled")) ||
        body.contains(QStringLiteral("characterRestPointOnReleaseNormalized")))
    {
        bool enabled = items[sceneIndex].characterRestPointOnReleaseEnabled;
        float normalized = items[sceneIndex].characterRestPointOnReleaseNormalized;
        if (body.contains(QStringLiteral("characterRestPointOnReleaseEnabled")))
        {
            enabled = body.value(QStringLiteral("characterRestPointOnReleaseEnabled")).toBool(enabled);
        }
        if (body.contains(QStringLiteral("characterRestPointOnReleaseNormalized")))
        {
            normalized = static_cast<float>(
                body.value(QStringLiteral("characterRestPointOnReleaseNormalized")).toDouble(normalized));
        }
        viewport->updateSceneItemCharacterRestPointOnRelease(sceneIndex, enabled, normalized);
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
        result.insert(QStringLiteral("translation"), QJsonArray{item.translation.x(), item.translation.y(), item.translation.z()});
        result.insert(QStringLiteral("rotation"), QJsonArray{item.rotation.x(), item.rotation.y(), item.rotation.z()});
        result.insert(QStringLiteral("scale"), QJsonArray{item.scale.x(), item.scale.y(), item.scale.z()});
        result.insert(QStringLiteral("meshConsolidationEnabled"), item.meshConsolidationEnabled);
        result.insert(QStringLiteral("focusPointOffset"), QJsonArray{item.focusPointOffset.x(), item.focusPointOffset.y(), item.focusPointOffset.z()});
        result.insert(QStringLiteral("focusDistance"), item.focusDistance);
        result.insert(QStringLiteral("focusCameraOffset"), QJsonArray{item.focusCameraOffset.x(), item.focusCameraOffset.y(), item.focusCameraOffset.z()});
        result.insert(QStringLiteral("focusCameraOffsetValid"), item.focusCameraOffsetValid);
        result.insert(QStringLiteral("activeAnimationClip"), item.activeAnimationClip);
        result.insert(QStringLiteral("animationPlaying"), item.animationPlaying);
        result.insert(QStringLiteral("animationLoop"), item.animationLoop);
        result.insert(QStringLiteral("animationSpeed"), item.animationSpeed);
        result.insert(QStringLiteral("animationCentroidNormalization"), item.animationCentroidNormalization);
        result.insert(QStringLiteral("animationTrimStartNormalized"), item.animationTrimStartNormalized);
        result.insert(QStringLiteral("animationTrimEndNormalized"), item.animationTrimEndNormalized);
        result.insert(QStringLiteral("characterRestPointOnReleaseEnabled"), item.characterRestPointOnReleaseEnabled);
        result.insert(QStringLiteral("characterRestPointOnReleaseNormalized"), item.characterRestPointOnReleaseNormalized);
        result.insert(QStringLiteral("isControllable"), viewport->isCharacterControlEnabled(sceneIndex));
        result.insert(QStringLiteral("freeFly"), viewport->isFreeFlyCameraEnabled());
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

    const AnimationMutation animationMutation = parseAnimationMutation(body, items[sceneIndex]);
    applyAnimationMutation(viewport, sceneIndex, animationMutation, true);

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
    const auto refreshedItems = viewport->sceneItems();
    if (sceneIndex >= 0 && sceneIndex < refreshedItems.size())
    {
        const auto& item = refreshedItems[sceneIndex];
        result.insert(QStringLiteral("clip"), item.activeAnimationClip);
        result.insert(QStringLiteral("playing"), item.animationPlaying);
        result.insert(QStringLiteral("loop"), item.animationLoop);
        result.insert(QStringLiteral("speed"), item.animationSpeed);
        result.insert(QStringLiteral("centroidNormalization"), item.animationCentroidNormalization);
        result.insert(QStringLiteral("trimStartNormalized"), item.animationTrimStartNormalized);
        result.insert(QStringLiteral("trimEndNormalized"), item.animationTrimEndNormalized);
    }
    else
    {
        result.insert(QStringLiteral("clip"), animationMutation.clip);
        result.insert(QStringLiteral("playing"), animationMutation.playing);
        result.insert(QStringLiteral("loop"), animationMutation.loop);
        result.insert(QStringLiteral("speed"), animationMutation.speed);
        result.insert(QStringLiteral("centroidNormalization"), animationMutation.centroidNormalization);
        result.insert(QStringLiteral("trimStartNormalized"), animationMutation.trimStartNormalized);
        result.insert(QStringLiteral("trimEndNormalized"), animationMutation.trimEndNormalized);
    }
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
        body.contains(QStringLiteral("keyShift")) ||
        body.contains(QStringLiteral("shift")) ||
        body.contains(QStringLiteral("sprint")) ||
        body.contains(QStringLiteral("jump")) ||
        body.contains(QStringLiteral("durationMs")) ||
        body.contains(QStringLiteral("move"));
    if (hasInputPayload)
    {
        bool keyW = body.value(QStringLiteral("keyW")).toBool(false);
        bool keyA = body.value(QStringLiteral("keyA")).toBool(false);
        bool keyS = body.value(QStringLiteral("keyS")).toBool(false);
        bool keyD = body.value(QStringLiteral("keyD")).toBool(false);
        const bool sprint =
            body.value(QStringLiteral("sprint")).toBool(
                body.value(QStringLiteral("shift")).toBool(
                    body.value(QStringLiteral("keyShift")).toBool(false)));
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

        const bool injected = viewport->injectCharacterInput(sceneIndex, keyW, keyA, keyS, keyD, jump, sprint, durationMs);
        result.insert(QStringLiteral("inputInjected"), injected);
        result.insert(QStringLiteral("keyW"), keyW);
        result.insert(QStringLiteral("keyA"), keyA);
        result.insert(QStringLiteral("keyS"), keyS);
        result.insert(QStringLiteral("keyD"), keyD);
        result.insert(QStringLiteral("sprint"), sprint);
        result.insert(QStringLiteral("jump"), jump);
        result.insert(QStringLiteral("durationMs"), durationMs);
    }

    result.insert(QStringLiteral("sceneIndex"), sceneIndex);
    result.insert(QStringLiteral("isControllable"), viewport->isCharacterControlEnabled(sceneIndex));
    return true;
}

bool ControlCommandService::handleLight(const QJsonObject& body, QJsonObject& result) const
{
    auto* viewport = m_window.viewportHost();
    if (!viewport)
    {
        result.insert(QStringLiteral("error"), QStringLiteral("viewport unavailable"));
        return false;
    }

    ViewportHostWidget::SceneLight light = viewport->sceneLight();

    if (body.value(QStringLiteral("create")).toBool(false) || !light.exists)
    {
        viewport->createSceneLight();
        light = viewport->sceneLight();
    }

    if (body.contains(QStringLiteral("exists")))
    {
        light.exists = body.value(QStringLiteral("exists")).toBool(light.exists);
    }

    if (body.contains(QStringLiteral("type")))
    {
        const QString type = body.value(QStringLiteral("type")).toString().trimmed().toLower();
        if (type == QStringLiteral("directional") ||
            type == QStringLiteral("ambient") ||
            type == QStringLiteral("point") ||
            type == QStringLiteral("spot") ||
            type == QStringLiteral("area") ||
            type == QStringLiteral("sun") ||
            type == QStringLiteral("hemispherical"))
        {
            light.type = type;
        }
        else
        {
            result.insert(QStringLiteral("error"),
                          QStringLiteral("type must be one of: directional, ambient, point, spot, area, sun, hemispherical"));
            return false;
        }
    }

    if (body.contains(QStringLiteral("brightness")))
    {
        light.brightness = static_cast<float>(body.value(QStringLiteral("brightness")).toDouble(light.brightness));
    }

    QVector3D proxyPosition = light.editorProxyPosition;
    if (body.contains(QStringLiteral("editorProxyPosition")) || body.contains(QStringLiteral("position")))
    {
        const QJsonArray value = body.contains(QStringLiteral("editorProxyPosition"))
            ? body.value(QStringLiteral("editorProxyPosition")).toArray()
            : body.value(QStringLiteral("position")).toArray();
        if (value.size() != 3)
        {
            result.insert(QStringLiteral("error"), QStringLiteral("editorProxyPosition must be an array of 3 numbers"));
            return false;
        }
        proxyPosition = QVector3D(
            static_cast<float>(value.at(0).toDouble(proxyPosition.x())),
            static_cast<float>(value.at(1).toDouble(proxyPosition.y())),
            static_cast<float>(value.at(2).toDouble(proxyPosition.z())));
    }
    if (body.contains(QStringLiteral("editorProxyPositionX")) || body.contains(QStringLiteral("positionX")))
    {
        const QJsonValue value = body.contains(QStringLiteral("editorProxyPositionX"))
            ? body.value(QStringLiteral("editorProxyPositionX"))
            : body.value(QStringLiteral("positionX"));
        proxyPosition.setX(static_cast<float>(value.toDouble(proxyPosition.x())));
    }
    if (body.contains(QStringLiteral("editorProxyPositionY")) || body.contains(QStringLiteral("positionY")))
    {
        const QJsonValue value = body.contains(QStringLiteral("editorProxyPositionY"))
            ? body.value(QStringLiteral("editorProxyPositionY"))
            : body.value(QStringLiteral("positionY"));
        proxyPosition.setY(static_cast<float>(value.toDouble(proxyPosition.y())));
    }
    if (body.contains(QStringLiteral("editorProxyPositionZ")) || body.contains(QStringLiteral("positionZ")))
    {
        const QJsonValue value = body.contains(QStringLiteral("editorProxyPositionZ"))
            ? body.value(QStringLiteral("editorProxyPositionZ"))
            : body.value(QStringLiteral("positionZ"));
        proxyPosition.setZ(static_cast<float>(value.toDouble(proxyPosition.z())));
    }
    if (body.contains(QStringLiteral("translation")) || body.contains(QStringLiteral("editorTranslation")))
    {
        const QJsonArray value = body.contains(QStringLiteral("editorTranslation"))
            ? body.value(QStringLiteral("editorTranslation")).toArray()
            : body.value(QStringLiteral("translation")).toArray();
        if (value.size() != 3)
        {
            result.insert(QStringLiteral("error"), QStringLiteral("editorTranslation must be an array of 3 numbers"));
            return false;
        }
        proxyPosition = QVector3D(
            static_cast<float>(value.at(0).toDouble(proxyPosition.x())),
            static_cast<float>(value.at(1).toDouble(proxyPosition.y())),
            static_cast<float>(value.at(2).toDouble(proxyPosition.z())));
    }
    light.editorProxyPosition = proxyPosition;

    QVector3D color = light.color;
    if (body.contains(QStringLiteral("color")))
    {
        const QJsonArray value = body.value(QStringLiteral("color")).toArray();
        if (value.size() != 3)
        {
            result.insert(QStringLiteral("error"), QStringLiteral("color must be an array of 3 numbers"));
            return false;
        }
        color = QVector3D(
            static_cast<float>(value.at(0).toDouble(color.x())),
            static_cast<float>(value.at(1).toDouble(color.y())),
            static_cast<float>(value.at(2).toDouble(color.z())));
    }
    if (body.contains(QStringLiteral("colorR")))
    {
        color.setX(static_cast<float>(body.value(QStringLiteral("colorR")).toDouble(color.x())));
    }
    if (body.contains(QStringLiteral("colorG")))
    {
        color.setY(static_cast<float>(body.value(QStringLiteral("colorG")).toDouble(color.y())));
    }
    if (body.contains(QStringLiteral("colorB")))
    {
        color.setZ(static_cast<float>(body.value(QStringLiteral("colorB")).toDouble(color.z())));
    }
    light.color = color;

    viewport->setSceneLight(light);

    const auto applied = viewport->sceneLight();
    result.insert(QStringLiteral("exists"), applied.exists);
    result.insert(QStringLiteral("type"), applied.type);
    result.insert(QStringLiteral("editorProxyPosition"), QJsonArray{applied.editorProxyPosition.x(), applied.editorProxyPosition.y(), applied.editorProxyPosition.z()});
    result.insert(QStringLiteral("position"), QJsonArray{applied.editorProxyPosition.x(), applied.editorProxyPosition.y(), applied.editorProxyPosition.z()});
    result.insert(QStringLiteral("brightness"), applied.brightness);
    result.insert(QStringLiteral("color"), QJsonArray{applied.color.x(), applied.color.y(), applied.color.z()});
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

bool ControlCommandService::handleBootstrapTps(const QJsonObject& body, QJsonObject& result) const
{
    auto* viewport = m_window.viewportHost();
    if (!viewport)
    {
        result.insert(QStringLiteral("error"), QStringLiteral("viewport unavailable"));
        return false;
    }

    const bool force = body.value(QStringLiteral("force")).toBool(false);
    result = viewport->bootstrapThirdPersonShooterReport(force);
    return true;
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
