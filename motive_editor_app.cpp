#include "motive_editor_app.h"
#include "asset_browser_widget.h"
#include "control_server.h"
#include "shell.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QDebug>
#include <QImageReader>
#include <QMetaObject>
#include <QProcess>
#include <QTimer>

#include <algorithm>
#include <limits>

namespace motive::ui {

int runMotiveEditorApp(int argc, char** argv)
{
    // Raise image allocation limit to allow 8192x8192 textures
    // 8192x8192 RGBA = 256MB, so set limit to 512MB to be safe
    QImageReader::setAllocationLimit(512);
    
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("MotiveEditor"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Motive editor"));
    parser.addHelpOption();
    QCommandLineOption controlPortOption(
        QStringList{QStringLiteral("control-port")},
        QStringLiteral("Control server port."),
        QStringLiteral("port"));
    parser.addOption(controlPortOption);
    parser.process(app);

    bool portOk = false;
    quint16 controlPort = 40132;
    const QString optionValue = parser.value(controlPortOption);
    if (!optionValue.isEmpty())
    {
        const uint parsed = optionValue.toUInt(&portOk);
        if (portOk && parsed <= std::numeric_limits<quint16>::max())
        {
            controlPort = static_cast<quint16>(parsed);
        }
    }
    else
    {
        const QString envValue = qEnvironmentVariable("EDITOR_CONTROL_PORT");
        const uint parsed = envValue.toUInt(&portOk);
        if (portOk && parsed <= std::numeric_limits<quint16>::max())
        {
            controlPort = static_cast<quint16>(parsed);
        }
    }

    MainWindowShell window;
    EngineUiControlServer controlServer(
        [&window]() -> QString
        {
            if (auto* browser = window.assetBrowser())
            {
                const QString root = browser->rootPath();
                if (!root.isEmpty())
                {
                    return root;
                }
            }
            return QDir::currentPath();
        },
        [&window]() -> EngineUiControlServer::ProfileData
        {
            EngineUiControlServer::ProfileData data;
            data.uiDebug = window.uiDebugJson();
            if (auto* browser = window.assetBrowser())
            {
                data.rootPath = browser->rootPath();
            }
            if (auto* viewport = window.viewportHost())
            {
                data.sceneItemCount = viewport->sceneItems().size();
                data.hierarchy = window.hierarchyJson();
                data.inspector = window.inspectorDebugJson();
                for (const auto& item : viewport->sceneProfileJson())
                {
                    data.sceneItems.append(item.toObject());
                }
                data.cameraPosition = viewport->cameraPosition();
                data.cameraRotation = viewport->cameraRotation();
                
                // Performance metrics
                const auto perf = viewport->performanceMetrics();
                data.currentFps = perf.currentFps;
                data.renderIntervalMs = perf.renderIntervalMs;
                data.renderTimerActive = perf.renderTimerActive;
                data.viewportWidth = perf.viewportWidth;
                data.viewportHeight = perf.viewportHeight;
                data.focusedViewportIndex = viewport->focusedViewportIndex();
                data.focusedViewportCameraId = viewport->focusedViewportCameraId();
                data.viewportCameraIds = viewport->viewportCameraIds();
                data.cameraTracking = viewport->cameraTrackingDebugJson();
            }
            return data;
        },
        [&window](const QString& command, const QJsonObject& body, QJsonObject& result) -> bool
        {
            if (command == QStringLiteral("window"))
            {
                const int width = body.value(QStringLiteral("width")).toInt(-1);
                const int height = body.value(QStringLiteral("height")).toInt(-1);
                const bool requestResize = width > 0 || height > 0;

                const bool invoked = QMetaObject::invokeMethod(
                    qApp,
                    [&window, width, height, requestResize, &result]()
                    {
                        if (requestResize)
                        {
                            const int targetWidth = width > 0 ? width : window.width();
                            const int targetHeight = height > 0 ? height : window.height();
                            window.resize(targetWidth, targetHeight);
                        }
                        result = window.uiDebugJson();
                    },
                    Qt::BlockingQueuedConnection);

                if (!invoked)
                {
                    result.insert(QStringLiteral("error"), QStringLiteral("failed to query window state"));
                    return false;
                }
                result.insert(QStringLiteral("requestedResize"), requestResize);
                return true;
            }

            auto* viewport = window.viewportHost();
            if (!viewport)
            {
                result.insert(QStringLiteral("error"), QStringLiteral("viewport unavailable"));
                return false;
            }

            if (command == QStringLiteral("primitive"))
            {
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

            if (command == QStringLiteral("scene_item"))
            {
                const int sceneIndex = body.value(QStringLiteral("sceneIndex")).toInt(-1);
                if (sceneIndex < 0)
                {
                    result.insert(QStringLiteral("error"), QStringLiteral("sceneIndex is required"));
                    return false;
                }
                if (body.contains(QStringLiteral("visible")))
                {
                    viewport->setSceneItemVisible(sceneIndex, body.value(QStringLiteral("visible")).toBool(true));
                }
                if (body.value(QStringLiteral("focus")).toBool(false))
                {
                    viewport->focusSceneItem(sceneIndex);
                }
                result.insert(QStringLiteral("sceneIndex"), sceneIndex);
                return true;
            }

            if (command == QStringLiteral("selection"))
            {
                bool selected = false;
                if (body.contains(QStringLiteral("sceneIndex")))
                {
                    selected = window.selectHierarchySceneItem(body.value(QStringLiteral("sceneIndex")).toInt(-1));
                }
                else if (body.contains(QStringLiteral("cameraId")) || body.contains(QStringLiteral("cameraIndex")))
                {
                    const QString cameraId = body.value(QStringLiteral("cameraId")).toString();
                    const int cameraIndex = body.value(QStringLiteral("cameraIndex")).toInt(-1);
                    selected = window.selectHierarchyCamera(cameraId, cameraIndex);
                }

                result = window.inspectorDebugJson();
                result.insert(QStringLiteral("selected"), selected);
                return selected;
            }

            if (command == QStringLiteral("animation"))
            {
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
                    window.selectHierarchySceneItem(sceneIndex);
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

            if (command == QStringLiteral("character"))
            {
                // Support both sceneIndex and name/label
                int sceneIndex = body.value(QStringLiteral("sceneIndex")).toInt(-1);
                const QString name = body.value(QStringLiteral("name")).toString();
                
                // If name provided, find matching sceneIndex
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
                
                // If no specific character requested, return list of controllable characters
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

            if (command == QStringLiteral("camera"))
            {
                const QStringList validCameraModes = {
                    QStringLiteral("FreeFly"),
                    QStringLiteral("CharacterFollow"),
                    QStringLiteral("OrbitFollow"),
                    QStringLiteral("Fixed")
                };
                auto resolveCameraIndex = [&](const QString& indexField, const QString& idField) -> int
                {
                    if (body.contains(idField))
                    {
                        const QString cameraId = body.value(idField).toString();
                        return viewport->cameraIndexForId(cameraId);
                    }
                    return body.value(indexField).toInt(-1);
                };

                if (body.contains(QStringLiteral("freeFly")))
                {
                    const bool enabled = body.value(QStringLiteral("freeFly")).toBool(true);
                    viewport->setFreeFlyCameraEnabled(enabled);
                    result.insert(QStringLiteral("freeFly"), enabled);
                }
                
                // List all cameras
                if (body.value(QStringLiteral("list")).toBool(false))
                {
                    QJsonArray cameras;
                    const auto configs = viewport->cameraConfigs();
                    for (int i = 0; i < configs.size(); ++i)
                    {
                        QJsonObject cam;
                        cam.insert(QStringLiteral("index"), i);
                        cam.insert(QStringLiteral("id"), configs[i].id);
                        cam.insert(QStringLiteral("name"), configs[i].name);
                        cam.insert(QStringLiteral("mode"), configs[i].mode);
                        cam.insert(QStringLiteral("type"), configs[i].isFollowCamera() ? QStringLiteral("follow") : QStringLiteral("free"));
                        cam.insert(QStringLiteral("followTargetIndex"), configs[i].followTargetIndex);
                        cam.insert(QStringLiteral("followDistance"), configs[i].followDistance);
                        cam.insert(QStringLiteral("followYaw"), configs[i].followYaw);
                        cam.insert(QStringLiteral("followPitch"), configs[i].followPitch);
                        cam.insert(QStringLiteral("followSmoothSpeed"), configs[i].followSmoothSpeed);
                        cam.insert(QStringLiteral("rotation"), QJsonArray{configs[i].rotation.x(), configs[i].rotation.y(), configs[i].rotation.z()});
                        cam.insert(QStringLiteral("position"), QJsonArray{configs[i].position.x(), configs[i].position.y(), configs[i].position.z()});
                        cameras.append(cam);
                    }
                    result.insert(QStringLiteral("cameras"), cameras);
                    result.insert(QStringLiteral("count"), configs.size());
                }
                
                // Create follow camera
                if (body.contains(QStringLiteral("createFollow")) || body.contains(QStringLiteral("createFollowName")))
                {
                    int sceneIndex = -1;
                    
                    // Try to resolve by name first
                    if (body.contains(QStringLiteral("createFollowName")))
                    {
                        const QString targetName = body.value(QStringLiteral("createFollowName")).toString();
                        const auto items = viewport->sceneItems();
                        for (int i = 0; i < items.size(); ++i)
                        {
                            if (items[i].name == targetName)
                            {
                                sceneIndex = i;
                                break;
                            }
                        }
                        if (sceneIndex < 0)
                        {
                            result.insert(QStringLiteral("error"), QStringLiteral("Scene item not found with name: %1").arg(targetName));
                            return false;
                        }
                    }
                    else
                    {
                        // Use integer index
                        sceneIndex = body.value(QStringLiteral("createFollow")).toInt(-1);
                    }
                    
                    const float distance = body.value(QStringLiteral("distance")).toDouble(5.0);
                    const float yaw = body.value(QStringLiteral("yaw")).toDouble(0.0);
                    const float pitch = body.value(QStringLiteral("pitch")).toDouble(20.0);
                    const int camIndex = viewport->createFollowCamera(sceneIndex, distance, yaw, pitch);
                    if (camIndex >= 0 && body.contains(QStringLiteral("smooth")))
                    {
                        auto configs = viewport->cameraConfigs();
                        if (camIndex < configs.size())
                        {
                            configs[camIndex].followSmoothSpeed =
                                std::max(0.0f, static_cast<float>(body.value(QStringLiteral("smooth")).toDouble(configs[camIndex].followSmoothSpeed)));
                            viewport->updateCameraConfig(camIndex, configs[camIndex]);
                        }
                    }
                    result.insert(QStringLiteral("created"), camIndex >= 0);
                    result.insert(QStringLiteral("cameraIndex"), camIndex);
                    result.insert(QStringLiteral("sceneIndex"), sceneIndex);
                }
                
                // Set active camera
                if (body.contains(QStringLiteral("setActive")) || body.contains(QStringLiteral("setActiveId")))
                {
                    const int cameraIndex = resolveCameraIndex(QStringLiteral("setActive"), QStringLiteral("setActiveId"));
                    if (cameraIndex >= 0)
                    {
                        viewport->setActiveCamera(cameraIndex);
                        result.insert(QStringLiteral("activeCamera"), cameraIndex);
                        result.insert(QStringLiteral("activeCameraId"), viewport->activeCameraId());
                    }
                    else
                    {
                        result.insert(QStringLiteral("error"), QStringLiteral("Camera not found"));
                        return false;
                    }
                }
                
                // Update camera config (e.g., change follow target)
                if (body.contains(QStringLiteral("updateCamera")) || body.contains(QStringLiteral("updateCameraId")))
                {
                    const int cameraIndex = resolveCameraIndex(QStringLiteral("updateCamera"), QStringLiteral("updateCameraId"));
                    if (cameraIndex >= 0)
                    {
                        auto configs = viewport->cameraConfigs();
                        if (cameraIndex < configs.size())
                        {
                            auto& config = configs[cameraIndex];
                            
                            // Update follow target - support both index and name
                            if (body.contains(QStringLiteral("followTargetIndex")) || body.contains(QStringLiteral("followTargetName")))
                            {
                                int targetIndex = -1;
                                
                                // Try to resolve by name first
                                if (body.contains(QStringLiteral("followTargetName")))
                                {
                                    const QString targetName = body.value(QStringLiteral("followTargetName")).toString();
                                    const auto items = viewport->sceneItems();
                                    for (int i = 0; i < items.size(); ++i)
                                    {
                                        if (items[i].name == targetName)
                                        {
                                            targetIndex = i;
                                            break;
                                        }
                                    }
                                    if (targetIndex < 0)
                                    {
                                        result.insert(QStringLiteral("error"), QStringLiteral("Scene item not found with name: %1").arg(targetName));
                                        return false;
                                    }
                                }
                                else
                                {
                                    // Use integer index
                                    targetIndex = body.value(QStringLiteral("followTargetIndex")).toInt(-1);
                                }
                                
                                config.followTargetIndex = targetIndex;
                                config.type = (config.followTargetIndex >= 0) 
                                    ? ViewportHostWidget::CameraConfig::Type::Follow 
                                    : ViewportHostWidget::CameraConfig::Type::Free;
                            }
                            
                            // Update follow distance
                            if (body.contains(QStringLiteral("distance")))
                            {
                                config.followDistance = body.value(QStringLiteral("distance")).toDouble(5.0);
                            }
                            
                            // Update follow angles
                            if (body.contains(QStringLiteral("yaw")))
                            {
                                config.followYaw = body.value(QStringLiteral("yaw")).toDouble(0.0);
                            }
                            if (body.contains(QStringLiteral("pitch")))
                            {
                                config.followPitch = body.value(QStringLiteral("pitch")).toDouble(20.0);
                            }
                            if (body.contains(QStringLiteral("smooth")))
                            {
                                config.followSmoothSpeed =
                                    std::max(0.0f, static_cast<float>(body.value(QStringLiteral("smooth")).toDouble(config.followSmoothSpeed)));
                            }
                            if (body.contains(QStringLiteral("mode")))
                            {
                                const QString mode = body.value(QStringLiteral("mode")).toString();
                                if (!validCameraModes.contains(mode))
                                {
                                    result.insert(QStringLiteral("error"), QStringLiteral("Invalid camera mode: %1").arg(mode));
                                    return false;
                                }
                                config.mode = mode;
                            }
                            
                            viewport->updateCameraConfig(cameraIndex, config);
                            result.insert(QStringLiteral("updated"), true);
                            result.insert(QStringLiteral("cameraIndex"), cameraIndex);
                            result.insert(QStringLiteral("cameraId"), configs[cameraIndex].id);
                        }
                        else
                        {
                            result.insert(QStringLiteral("error"), QStringLiteral("Camera index out of range"));
                            return false;
                        }
                    }
                    else
                    {
                        result.insert(QStringLiteral("error"), QStringLiteral("Camera not found"));
                        return false;
                    }
                }
                
                if (body.contains(QStringLiteral("setMode")) || body.contains(QStringLiteral("setModeId")))
                {
                    const int cameraIndex = resolveCameraIndex(QStringLiteral("setMode"), QStringLiteral("setModeId"));
                    if (cameraIndex < 0)
                    {
                        result.insert(QStringLiteral("error"), QStringLiteral("Camera not found"));
                        return false;
                    }
                    const QString mode = body.value(QStringLiteral("mode")).toString();
                    if (!validCameraModes.contains(mode))
                    {
                        result.insert(QStringLiteral("error"), QStringLiteral("mode is required and must be one of: FreeFly, CharacterFollow, OrbitFollow, Fixed"));
                        return false;
                    }
                    auto configs = viewport->cameraConfigs();
                    if (cameraIndex >= configs.size())
                    {
                        result.insert(QStringLiteral("error"), QStringLiteral("Camera index out of range"));
                        return false;
                    }
                    configs[cameraIndex].mode = mode;
                    viewport->updateCameraConfig(cameraIndex, configs[cameraIndex]);
                    result.insert(QStringLiteral("updated"), true);
                    result.insert(QStringLiteral("cameraIndex"), cameraIndex);
                    result.insert(QStringLiteral("cameraId"), configs[cameraIndex].id);
                    result.insert(QStringLiteral("mode"), mode);
                }

                result.insert(QStringLiteral("freeFly"), viewport->isFreeFlyCameraEnabled());
                return true;
            }

            if (command == QStringLiteral("rebuild"))
            {
                // Rebuild the viewport/hierarchy
                // This refreshes the hierarchy and triggers any pending updates
                viewport->refresh();
                result.insert(QStringLiteral("ok"), true);
                result.insert(QStringLiteral("message"), QStringLiteral("Scene rebuilt"));
                return true;
            }

            if (command == QStringLiteral("reset"))
            {
                // Reset cameras to default state
                const QString resetType = body.value(QStringLiteral("type")).toString(QStringLiteral("cameras"));
                
                if (resetType == QStringLiteral("cameras"))
                {
                    // Reset to single default camera
                    auto configs = viewport->cameraConfigs();
                    while (configs.size() > 1)
                    {
                        viewport->deleteCamera(configs.size() - 1);
                        configs = viewport->cameraConfigs();
                    }
                    
                    // Reset the remaining camera to default position
                    if (configs.size() == 1)
                    {
                        ViewportHostWidget::CameraConfig defaultConfig;
                        defaultConfig.name = QStringLiteral("Camera");
                        defaultConfig.type = ViewportHostWidget::CameraConfig::Type::Free;
                        defaultConfig.position = QVector3D(0.0f, 0.0f, 3.0f);
                        defaultConfig.rotation = QVector3D(0.0f, 0.0f, 0.0f);
                        viewport->updateCameraConfig(0, defaultConfig);
                        viewport->setActiveCamera(0);
                    }
                    
                    result.insert(QStringLiteral("ok"), true);
                    result.insert(QStringLiteral("type"), QStringLiteral("cameras"));
                    result.insert(QStringLiteral("message"), QStringLiteral("Cameras reset to default"));
                }
                else if (resetType == QStringLiteral("all"))
                {
                    // Reset everything - cameras and scene items
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
                        viewport->updateCameraConfig(0, defaultConfig);
                        viewport->setActiveCamera(0);
                    }
                    
                    // Also clear scene items
                    viewport->loadSceneFromItems(QList<ViewportHostWidget::SceneItem>{});
                    
                    result.insert(QStringLiteral("ok"), true);
                    result.insert(QStringLiteral("type"), QStringLiteral("all"));
                    result.insert(QStringLiteral("message"), QStringLiteral("Scene and cameras reset to default"));
                }
                else
                {
                    result.insert(QStringLiteral("error"), QStringLiteral("Unknown reset type. Use 'cameras' or 'all'"));
                    return false;
                }
                
                return true;
            }

            if (command == QStringLiteral("physics_coupling"))
            {
                const int sceneIndex = body.value(QStringLiteral("sceneIndex")).toInt(-1);
                if (sceneIndex < 0)
                {
                    result.insert(QStringLiteral("error"), QStringLiteral("sceneIndex is required"));
                    return false;
                }
                
                // Get current coupling mode
                const auto items = viewport->sceneItems();
                if (sceneIndex >= items.size())
                {
                    result.insert(QStringLiteral("error"), QStringLiteral("sceneIndex %1 out of range (have %2 items)").arg(sceneIndex).arg(items.size()));
                    return false;
                }
                
                // Set coupling if provided
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
                    // Return current mode
                    result.insert(QStringLiteral("mode"), items[sceneIndex].animationPhysicsCoupling);
                }
                
                result.insert(QStringLiteral("sceneIndex"), sceneIndex);
                return true;
            }

            if (command == QStringLiteral("physics_gravity"))
            {
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
                
                // Update useGravity if provided
                if (body.contains(QStringLiteral("useGravity")))
                {
                    useGravity = body.value(QStringLiteral("useGravity")).toBool(true);
                }
                
                // Update custom gravity if provided
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

            result.insert(QStringLiteral("error"), QStringLiteral("unknown command"));
            return false;
        },
        [&app]() -> bool
        {
            qDebug() << "[RESTART] Build and restart initiated via REST API";
            
            // Run build in background
            int buildResult = system("cd /mnt/Cancer/PanelVid2TikTok/motive3d && python build.py --rebuild > /tmp/motive_build.log 2>&1");
            
            if (buildResult != 0)
            {
                qWarning() << "[RESTART] Build failed with code" << buildResult;
                return false;
            }
            
            qDebug() << "[RESTART] Build successful, restarting application...";
            
            // Schedule app restart via Qt
            QTimer::singleShot(500, qApp, []() {
                QProcess::startDetached(qApp->arguments()[0], qApp->arguments());
                qApp->quit();
            });
            
            return true;
        },
        &window);

    const bool controlServerStarted = controlServer.start(controlPort);
    if (!controlServerStarted)
    {
        qCritical() << "[STARTUP] Control server failed to initialize on port" << controlPort
                    << "- exiting";
        return 1;
    }
    qInfo() << "[STARTUP] Control server initialized on port" << controlPort;

    window.show();

    return app.exec();
}

}  // namespace motive::ui
