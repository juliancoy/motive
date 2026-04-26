#include "control_command_service.h"

#include "host_widget.h"
#include "shell.h"

namespace motive::ui {

bool ControlCommandService::handleDebugMotion(const QJsonObject& body, QJsonObject& result) const
{
    auto* viewport = m_window.viewportHost();
    if (!viewport)
    {
        result.insert(QStringLiteral("error"), QStringLiteral("viewport unavailable"));
        return false;
    }

    const QString action = body.value(QStringLiteral("action")).toString(QStringLiteral("summary"));
    if (action == QStringLiteral("reset"))
    {
        viewport->resetMotionDebug();
        result.insert(QStringLiteral("action"), action);
        result.insert(QStringLiteral("summary"), viewport->motionDebugSummaryJson());
        return true;
    }

    result.insert(QStringLiteral("action"), action);
    result.insert(QStringLiteral("frame"), viewport->motionDebugFrameJson());
    result.insert(QStringLiteral("summary"), viewport->motionDebugSummaryJson());
    return true;
}

bool ControlCommandService::handleMotionDebugHistory(const QJsonObject& body, QJsonObject& result) const
{
    auto* viewport = m_window.viewportHost();
    if (!viewport)
    {
        result.insert(QStringLiteral("error"), QStringLiteral("viewport unavailable"));
        return false;
    }

    const int maxFrames = body.value(QStringLiteral("maxFrames")).toInt(300);
    const int sceneIndex = body.value(QStringLiteral("sceneIndex")).toInt(-1);
    result.insert(QStringLiteral("maxFrames"), maxFrames);
    result.insert(QStringLiteral("sceneIndex"), sceneIndex);
    result.insert(QStringLiteral("history"), viewport->motionDebugHistoryJson(maxFrames, sceneIndex));
    result.insert(QStringLiteral("frame"), viewport->motionDebugFrameJson());
    result.insert(QStringLiteral("summary"), viewport->motionDebugSummaryJson());
    return true;
}

bool ControlCommandService::handleMotionDebugSummary(QJsonObject& result) const
{
    auto* viewport = m_window.viewportHost();
    if (!viewport)
    {
        result.insert(QStringLiteral("error"), QStringLiteral("viewport unavailable"));
        return false;
    }

    result = viewport->motionDebugSummaryJson();
    result.insert(QStringLiteral("frame"), viewport->motionDebugFrameJson());
    return true;
}

bool ControlCommandService::handleMotionDebugSummaryCommand(const QJsonObject& body, QJsonObject& result) const
{
    Q_UNUSED(body);
    return handleMotionDebugSummary(result);
}

bool ControlCommandService::handleMotionDebugOverlay(const QJsonObject& body, QJsonObject& result) const
{
    auto* viewport = m_window.viewportHost();
    if (!viewport)
    {
        result.insert(QStringLiteral("error"), QStringLiteral("viewport unavailable"));
        return false;
    }

    ViewportHostWidget::MotionDebugOverlayOptions options;
    const QJsonObject current = viewport->motionDebugOverlayOptionsJson();
    options.enabled = current.value(QStringLiteral("enabled")).toBool(false);
    options.showTargetMarkers = current.value(QStringLiteral("showTargetMarkers")).toBool(true);
    options.showVelocityVector = current.value(QStringLiteral("showVelocityVector")).toBool(true);
    options.showCameraToTargetLine = current.value(QStringLiteral("showCameraToTargetLine")).toBool(true);
    options.showScreenCenterCrosshair = current.value(QStringLiteral("showScreenCenterCrosshair")).toBool(true);
    options.showMotionTrail = current.value(QStringLiteral("showMotionTrail")).toBool(true);
    options.showRawTrail = current.value(QStringLiteral("showRawTrail")).toBool(false);
    options.trailFrames = current.value(QStringLiteral("trailFrames")).toInt(32);
    options.velocityScale = static_cast<float>(current.value(QStringLiteral("velocityScale")).toDouble(0.25));

    if (body.contains(QStringLiteral("enabled")))
    {
        options.enabled = body.value(QStringLiteral("enabled")).toBool(options.enabled);
    }
    if (body.contains(QStringLiteral("showTargetMarkers")))
    {
        options.showTargetMarkers = body.value(QStringLiteral("showTargetMarkers")).toBool(options.showTargetMarkers);
    }
    if (body.contains(QStringLiteral("showVelocityVector")))
    {
        options.showVelocityVector = body.value(QStringLiteral("showVelocityVector")).toBool(options.showVelocityVector);
    }
    if (body.contains(QStringLiteral("showCameraToTargetLine")))
    {
        options.showCameraToTargetLine = body.value(QStringLiteral("showCameraToTargetLine")).toBool(options.showCameraToTargetLine);
    }
    if (body.contains(QStringLiteral("showScreenCenterCrosshair")))
    {
        options.showScreenCenterCrosshair = body.value(QStringLiteral("showScreenCenterCrosshair")).toBool(options.showScreenCenterCrosshair);
    }
    if (body.contains(QStringLiteral("showMotionTrail")))
    {
        options.showMotionTrail = body.value(QStringLiteral("showMotionTrail")).toBool(options.showMotionTrail);
    }
    if (body.contains(QStringLiteral("showRawTrail")))
    {
        options.showRawTrail = body.value(QStringLiteral("showRawTrail")).toBool(options.showRawTrail);
    }
    if (body.contains(QStringLiteral("trailFrames")))
    {
        options.trailFrames = body.value(QStringLiteral("trailFrames")).toInt(options.trailFrames);
    }
    if (body.contains(QStringLiteral("velocityScale")))
    {
        options.velocityScale = static_cast<float>(body.value(QStringLiteral("velocityScale")).toDouble(options.velocityScale));
    }

    viewport->setMotionDebugOverlayOptions(options);
    result = viewport->motionDebugOverlayOptionsJson();
    return true;
}

}  // namespace motive::ui
