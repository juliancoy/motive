#include "engine_ui_motive_editor_app.h"
#include "engine_ui_asset_browser_widget.h"
#include "engine_ui_control_server.h"
#include "engine_ui_main_window_shell.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QDebug>

#include <limits>

namespace motive::ui {

int runMotiveEditorApp(int argc, char** argv)
{
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
