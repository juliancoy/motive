#pragma once

#include <QJsonObject>
#include <QString>

namespace motive::ui {

class MainWindowShell;

// Handles control-server command branches extracted from motive_editor_app wiring.
class ControlCommandService
{
public:
    explicit ControlCommandService(MainWindowShell& window);

    // Returns true when command was handled successfully.
    // Sets handled=true when command is recognized (success or failure).
    bool tryHandle(const QString& command,
                   const QJsonObject& body,
                   QJsonObject& result,
                   bool& handled) const;

private:
    using CommandHandler = bool (ControlCommandService::*)(const QJsonObject&, QJsonObject&) const;

    bool tryHandleSelectionDomain(const QString& command, const QJsonObject& body, QJsonObject& result, bool& handled) const;
    bool tryHandleCameraDomain(const QString& command, const QJsonObject& body, QJsonObject& result, bool& handled) const;
    bool tryHandleWindowDomain(const QString& command, const QJsonObject& body, QJsonObject& result, bool& handled) const;
    bool tryHandleMotionDomain(const QString& command, const QJsonObject& body, QJsonObject& result, bool& handled) const;
    bool tryHandleSceneDomain(const QString& command, const QJsonObject& body, QJsonObject& result, bool& handled) const;
    bool tryHandlePhysicsDomain(const QString& command, const QJsonObject& body, QJsonObject& result, bool& handled) const;

    bool handleSelection(const QJsonObject& body, QJsonObject& result) const;
    bool handleCamera(const QJsonObject& body, QJsonObject& result) const;
    bool handleWindow(const QJsonObject& body, QJsonObject& result) const;
    bool handleDebugMotion(const QJsonObject& body, QJsonObject& result) const;
    bool handleMotionDebugHistory(const QJsonObject& body, QJsonObject& result) const;
    bool handleMotionDebugSummary(QJsonObject& result) const;
    bool handleMotionDebugOverlay(const QJsonObject& body, QJsonObject& result) const;
    bool handlePrimitive(const QJsonObject& body, QJsonObject& result) const;
    bool handlePlaneIndicators(const QJsonObject& body, QJsonObject& result) const;
    bool handleSceneItem(const QJsonObject& body, QJsonObject& result) const;
    bool handleAnimation(const QJsonObject& body, QJsonObject& result) const;
    bool handleCharacter(const QJsonObject& body, QJsonObject& result) const;
    bool handleLight(const QJsonObject& body, QJsonObject& result) const;
    bool handleRebuild(QJsonObject& result) const;
    bool handleReset(const QJsonObject& body, QJsonObject& result) const;
    bool handleBootstrapTps(const QJsonObject& body, QJsonObject& result) const;
    bool handlePhysicsCoupling(const QJsonObject& body, QJsonObject& result) const;
    bool handlePhysicsGravity(const QJsonObject& body, QJsonObject& result) const;
    bool handleMotionDebugSummaryCommand(const QJsonObject& body, QJsonObject& result) const;
    bool handleRebuildCommand(const QJsonObject& body, QJsonObject& result) const;

    MainWindowShell& m_window;
};

}  // namespace motive::ui
