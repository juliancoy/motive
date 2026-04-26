#include "control_command_service.h"

#include "host_widget.h"
#include "shell.h"

#include <QStringList>
#include <QVector3D>

namespace motive::ui {

bool ControlCommandService::handlePhysicsCoupling(const QJsonObject& body, QJsonObject& result) const
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
        result.insert(QStringLiteral("error"), QStringLiteral("sceneIndex %1 out of range (have %2 items)").arg(sceneIndex).arg(items.size()));
        return false;
    }

    if (body.contains(QStringLiteral("mode")))
    {
        const QString mode = body.value(QStringLiteral("mode")).toString();
        const QStringList validModes = {
            QStringLiteral("AnimationOnly"),
            QStringLiteral("Kinematic"),
            QStringLiteral("RootMotionPhysics"),
            QStringLiteral("PhysicsDriven"),
            QStringLiteral("Ragdoll"),
            QStringLiteral("PartialRagdoll"),
            QStringLiteral("ActiveRagdoll")
        };

        if (!validModes.contains(mode))
        {
            result.insert(QStringLiteral("error"), QStringLiteral("Invalid mode: %1").arg(mode));
            return false;
        }

        viewport->updateSceneItemAnimationPhysicsCoupling(sceneIndex, mode);
        result.insert(QStringLiteral("mode"), mode);
    }
    else
    {
        result.insert(QStringLiteral("mode"), items[sceneIndex].animationPhysicsCoupling);
    }

    result.insert(QStringLiteral("sceneIndex"), sceneIndex);
    return true;
}

bool ControlCommandService::handlePhysicsGravity(const QJsonObject& body, QJsonObject& result) const
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

    bool useGravity = items[sceneIndex].useGravity;
    QVector3D customGravity = items[sceneIndex].customGravity;

    if (body.contains(QStringLiteral("useGravity")))
    {
        useGravity = body.value(QStringLiteral("useGravity")).toBool(true);
    }

    if (body.contains(QStringLiteral("gravityX")) || body.contains(QStringLiteral("gravityY")) || body.contains(QStringLiteral("gravityZ")))
    {
        customGravity.setX(static_cast<float>(body.value(QStringLiteral("gravityX")).toDouble(customGravity.x())));
        customGravity.setY(static_cast<float>(body.value(QStringLiteral("gravityY")).toDouble(customGravity.y())));
        customGravity.setZ(static_cast<float>(body.value(QStringLiteral("gravityZ")).toDouble(customGravity.z())));
    }

    viewport->updateSceneItemPhysicsGravity(sceneIndex, useGravity, customGravity);

    result.insert(QStringLiteral("sceneIndex"), sceneIndex);
    result.insert(QStringLiteral("useGravity"), useGravity);
    result.insert(QStringLiteral("customGravity"), QJsonObject{
        {QStringLiteral("x"), customGravity.x()},
        {QStringLiteral("y"), customGravity.y()},
        {QStringLiteral("z"), customGravity.z()}
    });
    return true;
}

}  // namespace motive::ui
