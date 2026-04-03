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
                for (const auto& item : viewport->sceneItems())
                {
                    data.sceneItems.append(QJsonObject{
                        {QStringLiteral("name"), item.name},
                        {QStringLiteral("sourcePath"), item.sourcePath},
                        {QStringLiteral("translation"), QJsonArray{item.translation.x(), item.translation.y(), item.translation.z()}},
                        {QStringLiteral("rotation"), QJsonArray{item.rotation.x(), item.rotation.y(), item.rotation.z()}},
                        {QStringLiteral("scale"), QJsonArray{item.scale.x(), item.scale.y(), item.scale.z()}}
                    });
                }
                data.cameraPosition = viewport->cameraPosition();
                data.cameraRotation = viewport->cameraRotation();
            }
            return data;
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
