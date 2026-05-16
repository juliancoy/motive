#include "control_command_service.h"

#include "shell.h"

#include <array>

namespace motive::ui {

ControlCommandService::ControlCommandService(MainWindowShell& window)
    : m_window(window)
{
}

bool ControlCommandService::tryHandle(const QString& command,
                                      const QJsonObject& body,
                                      QJsonObject& result,
                                      bool& handled) const
{
    handled = false;
    if (tryHandleSelectionDomain(command, body, result, handled))
    {
        return true;
    }
    if (handled)
    {
        return false;
    }
    if (tryHandleCameraDomain(command, body, result, handled))
    {
        return true;
    }
    if (handled)
    {
        return false;
    }
    if (tryHandleWindowDomain(command, body, result, handled))
    {
        return true;
    }
    if (handled)
    {
        return false;
    }
    if (tryHandleMotionDomain(command, body, result, handled))
    {
        return true;
    }
    if (handled)
    {
        return false;
    }
    if (tryHandleSceneDomain(command, body, result, handled))
    {
        return true;
    }
    if (handled)
    {
        return false;
    }
    return tryHandlePhysicsDomain(command, body, result, handled);
}

bool ControlCommandService::tryHandleSelectionDomain(const QString& command,
                                                     const QJsonObject& body,
                                                     QJsonObject& result,
                                                     bool& handled) const
{
    struct Entry
    {
        const char* name;
        CommandHandler handler;
    };
    static const std::array<Entry, 1> kSelectionCommands{{
        {"selection", &ControlCommandService::handleSelection},
    }};
    for (const Entry& entry : kSelectionCommands)
    {
        if (command == QLatin1String(entry.name))
        {
            handled = true;
            return (this->*(entry.handler))(body, result);
        }
    }
    return false;
}

bool ControlCommandService::tryHandleCameraDomain(const QString& command,
                                                  const QJsonObject& body,
                                                  QJsonObject& result,
                                                  bool& handled) const
{
    struct Entry
    {
        const char* name;
        CommandHandler handler;
    };
    static const std::array<Entry, 1> kCameraCommands{{
        {"camera", &ControlCommandService::handleCamera},
    }};
    for (const Entry& entry : kCameraCommands)
    {
        if (command == QLatin1String(entry.name))
        {
            handled = true;
            return (this->*(entry.handler))(body, result);
        }
    }
    return false;
}

bool ControlCommandService::tryHandleWindowDomain(const QString& command,
                                                  const QJsonObject& body,
                                                  QJsonObject& result,
                                                  bool& handled) const
{
    if (command == QLatin1String("window"))
    {
        handled = true;
        return handleWindow(body, result);
    }
    return false;
}

bool ControlCommandService::tryHandleMotionDomain(const QString& command,
                                                  const QJsonObject& body,
                                                  QJsonObject& result,
                                                  bool& handled) const
{
    struct Entry
    {
        const char* name;
        CommandHandler handler;
    };
    static const std::array<Entry, 4> kMotionCommands{{
        {"debug_motion", &ControlCommandService::handleDebugMotion},
        {"motion_debug_history", &ControlCommandService::handleMotionDebugHistory},
        {"motion_debug_summary", &ControlCommandService::handleMotionDebugSummaryCommand},
        {"motion_debug_overlay", &ControlCommandService::handleMotionDebugOverlay},
    }};
    for (const Entry& entry : kMotionCommands)
    {
        if (command == QLatin1String(entry.name))
        {
            handled = true;
            return (this->*(entry.handler))(body, result);
        }
    }
    return false;
}

bool ControlCommandService::tryHandleSceneDomain(const QString& command,
                                                 const QJsonObject& body,
                                                 QJsonObject& result,
                                                 bool& handled) const
{
    struct Entry
    {
        const char* name;
        CommandHandler handler;
    };
    static const std::array<Entry, 9> kSceneCommands{{
        {"primitive", &ControlCommandService::handlePrimitive},
        {"plane_indicators", &ControlCommandService::handlePlaneIndicators},
        {"scene_item", &ControlCommandService::handleSceneItem},
        {"animation", &ControlCommandService::handleAnimation},
        {"character", &ControlCommandService::handleCharacter},
        {"light", &ControlCommandService::handleLight},
        {"rebuild", &ControlCommandService::handleRebuildCommand},
        {"bootstrap_tps", &ControlCommandService::handleBootstrapTps},
        {"reset", &ControlCommandService::handleReset},
    }};
    for (const Entry& entry : kSceneCommands)
    {
        if (command == QLatin1String(entry.name))
        {
            handled = true;
            return (this->*(entry.handler))(body, result);
        }
    }
    return false;
}

bool ControlCommandService::tryHandlePhysicsDomain(const QString& command,
                                                   const QJsonObject& body,
                                                   QJsonObject& result,
                                                   bool& handled) const
{
    struct Entry
    {
        const char* name;
        CommandHandler handler;
    };
    static const std::array<Entry, 2> kPhysicsCommands{{
        {"physics_coupling", &ControlCommandService::handlePhysicsCoupling},
        {"physics_gravity", &ControlCommandService::handlePhysicsGravity},
    }};
    for (const Entry& entry : kPhysicsCommands)
    {
        if (command == QLatin1String(entry.name))
        {
            handled = true;
            return (this->*(entry.handler))(body, result);
        }
    }
    return false;
}

bool ControlCommandService::handleSelection(const QJsonObject& body, QJsonObject& result) const
{
    if (!body.contains(QStringLiteral("sceneIndex")) &&
        !body.value(QStringLiteral("light")).toBool(false) &&
        body.value(QStringLiteral("target")).toString().compare(QStringLiteral("light"), Qt::CaseInsensitive) != 0 &&
        !body.contains(QStringLiteral("cameraId")) &&
        !body.contains(QStringLiteral("cameraIndex")))
    {
        result.insert(QStringLiteral("error"), QStringLiteral("sceneIndex, light target, or cameraId/cameraIndex is required"));
        return false;
    }

    bool selected = false;
    const bool focusRequested = body.value(QStringLiteral("focus")).toBool(false);
    if (body.value(QStringLiteral("light")).toBool(false) ||
        body.value(QStringLiteral("target")).toString().compare(QStringLiteral("light"), Qt::CaseInsensitive) == 0)
    {
        selected = m_window.selectHierarchyLight();
    }
    else if (body.contains(QStringLiteral("sceneIndex")))
    {
        const int sceneIndex = body.value(QStringLiteral("sceneIndex")).toInt(-1);
        selected = m_window.selectHierarchySceneItem(sceneIndex);
        if (selected && focusRequested)
        {
            if (auto* viewport = m_window.viewportHost())
            {
                viewport->focusSceneItem(sceneIndex);
            }
        }
    }
    else if (body.contains(QStringLiteral("cameraId")) || body.contains(QStringLiteral("cameraIndex")))
    {
        const QString cameraId = body.value(QStringLiteral("cameraId")).toString();
        const int cameraIndex = body.value(QStringLiteral("cameraIndex")).toInt(-1);
        selected = m_window.selectHierarchyCamera(cameraId, cameraIndex);
    }

    result = m_window.inspectorDebugJson();
    result.insert(QStringLiteral("selected"), selected);
    if (!selected)
    {
        result.insert(QStringLiteral("message"), QStringLiteral("selection target not available yet"));
    }
    return true;
}

bool ControlCommandService::handleWindow(const QJsonObject& body, QJsonObject& result) const
{
    auto* viewport = m_window.viewportHost();
    if (!viewport)
    {
        result.insert(QStringLiteral("error"), QStringLiteral("viewport not available"));
        return false;
    }

    bool actionTaken = false;
    if (body.value(QStringLiteral("focusViewport")).toBool(false) ||
        body.value(QStringLiteral("focusNativeViewport")).toBool(false))
    {
        result.insert(QStringLiteral("focusViewportApplied"), viewport->focusViewportNativeWindow());
        actionTaken = true;
    }

    if (body.value(QStringLiteral("clearInput")).toBool(false))
    {
        viewport->clearRuntimeInputState();
        result.insert(QStringLiteral("clearInputApplied"), true);
        actionTaken = true;
    }

    result.insert(QStringLiteral("windowDebug"), viewport->windowDebugJson());
    if (!actionTaken)
    {
        result.insert(QStringLiteral("message"), QStringLiteral("no window action requested; returned debug snapshot"));
    }
    return true;
}

}  // namespace motive::ui
