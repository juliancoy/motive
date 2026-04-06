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
    quint16 controlPort = 40130;
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
            if (auto* browser = window.assetBrowser())
            {
                data.rootPath = browser->rootPath();
            }
            if (auto* viewport = window.viewportHost())
            {
                data.sceneItemCount = viewport->sceneItems().size();
                data.hierarchy = window.hierarchyJson();
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
            }
            return data;
        },
        [&window](const QString& command, const QJsonObject& body, QJsonObject& result) -> bool
        {
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
                
                result.insert(QStringLiteral("sceneIndex"), sceneIndex);
                result.insert(QStringLiteral("isControllable"), viewport->isCharacterControlEnabled(sceneIndex));
                return true;
            }

            if (command == QStringLiteral("camera"))
            {
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
                        cam.insert(QStringLiteral("name"), configs[i].name);
                        cam.insert(QStringLiteral("type"), configs[i].isFollowCamera() ? QStringLiteral("follow") : QStringLiteral("free"));
                        cam.insert(QStringLiteral("followTargetIndex"), configs[i].followTargetIndex);
                        cam.insert(QStringLiteral("position"), QJsonArray{configs[i].position.x(), configs[i].position.y(), configs[i].position.z()});
                        cameras.append(cam);
                    }
                    result.insert(QStringLiteral("cameras"), cameras);
                    result.insert(QStringLiteral("count"), configs.size());
                }
                
                // Create follow camera
                if (body.contains(QStringLiteral("createFollow")))
                {
                    const int sceneIndex = body.value(QStringLiteral("createFollow")).toInt(-1);
                    const float distance = body.value(QStringLiteral("distance")).toDouble(5.0);
                    const float yaw = body.value(QStringLiteral("yaw")).toDouble(0.0);
                    const float pitch = body.value(QStringLiteral("pitch")).toDouble(20.0);
                    const int camIndex = viewport->createFollowCamera(sceneIndex, distance, yaw, pitch);
                    result.insert(QStringLiteral("created"), camIndex >= 0);
                    result.insert(QStringLiteral("cameraIndex"), camIndex);
                    result.insert(QStringLiteral("sceneIndex"), sceneIndex);
                }
                
                // Set active camera
                if (body.contains(QStringLiteral("setActive")))
                {
                    const int cameraIndex = body.value(QStringLiteral("setActive")).toInt(-1);
                    viewport->setActiveCamera(cameraIndex);
                    result.insert(QStringLiteral("activeCamera"), cameraIndex);
                }
                
                // Update camera config (e.g., change follow target)
                if (body.contains(QStringLiteral("updateCamera")))
                {
                    const int cameraIndex = body.value(QStringLiteral("updateCamera")).toInt(-1);
                    if (cameraIndex >= 0)
                    {
                        auto configs = viewport->cameraConfigs();
                        if (cameraIndex < configs.size())
                        {
                            auto& config = configs[cameraIndex];
                            
                            // Update follow target
                            if (body.contains(QStringLiteral("followTargetIndex")))
                            {
                                config.followTargetIndex = body.value(QStringLiteral("followTargetIndex")).toInt(-1);
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
                            
                            viewport->updateCameraConfig(cameraIndex, config);
                            result.insert(QStringLiteral("updated"), true);
                            result.insert(QStringLiteral("cameraIndex"), cameraIndex);
                        }
                    }
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
        &window);

    const bool controlServerStarted = controlServer.start(controlPort);
    if (controlServerStarted)
    {
        qInfo() << "[STARTUP] Control server initialized on port" << controlPort;
    }
    else
    {
        qWarning() << "[STARTUP] Control server failed to initialize on port" << controlPort;
    }

    window.show();

    return app.exec();
}

}  // namespace motive::ui
