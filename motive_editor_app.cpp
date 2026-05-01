#include "motive_editor_app.h"
#include "asset_browser_widget.h"
#include "control_command_service.h"
#include "control_server.h"
#include "profile_data_service.h"
#include "shell.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QDebug>
#include <QImageReader>
#include <QMetaObject>
#include <QPixmap>
#include <QProcess>
#include <QThread>
#include <QTimer>

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
    QCommandLineOption authApiOption(
        QStringList{QStringLiteral("auth-api")},
        QStringLiteral("Authentication API base URL."),
        QStringLiteral("url"));
    QCommandLineOption guestModeOption(
        QStringList{QStringLiteral("guest-mode")},
        QStringLiteral("Start directly in guest mode (skip login prompt)."));
    parser.addOption(controlPortOption);
    parser.addOption(authApiOption);
    parser.addOption(guestModeOption);
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

    const QString configuredAuthApi =
        parser.isSet(authApiOption)
            ? parser.value(authApiOption).trimmed()
            : qEnvironmentVariable("MOTIVE_AUTH_API_BASE_URL").trimmed();
    QString authApiBaseUrl = configuredAuthApi;
    if (authApiBaseUrl.isEmpty())
    {
        authApiBaseUrl = QStringLiteral(MOTIVE_AUTH_API_BASE_URL).trimmed();
    }
    const bool forceGuestMode = parser.isSet(guestModeOption);

    if (!authApiBaseUrl.isEmpty())
    {
        qputenv("MOTIVE_AUTH_API_BASE_URL", authApiBaseUrl.toUtf8());
    }

    if (forceGuestMode)
    {
        qInfo() << "[AUTH] Startup login prompt disabled; --guest-mode has no startup effect.";
    }
    else
    {
        qInfo() << "[AUTH] Startup login prompt disabled. Use the profile menu to sign in via CPPMonetize.";
    }

    MainWindowShell window;
    ProfileDataService profileDataService(window);
    ControlCommandService controlCommandService(window);
    EngineUiControlServer controlServer(
        [&profileDataService]() -> QString
        {
            return profileDataService.resolveRootPath();
        },
        [&profileDataService]() -> EngineUiControlServer::ProfileData
        {
            return profileDataService.captureProfileData();
        },
        [&window, &controlCommandService](const QString& command, const QJsonObject& body, QJsonObject& result) -> bool
        {
            if (command == QStringLiteral("window"))
            {
                const int width = body.value(QStringLiteral("width")).toInt(-1);
                const int height = body.value(QStringLiteral("height")).toInt(-1);
                const bool requestResize = width > 0 || height > 0;
                const QString snapshotPath = body.value(QStringLiteral("snapshotPath")).toString();
                const bool requestSnapshot = !snapshotPath.isEmpty();
                const bool viewportOnly = body.value(QStringLiteral("viewportOnly")).toBool(false);

                const Qt::ConnectionType invokeType =
                    (QThread::currentThread() == qApp->thread())
                        ? Qt::DirectConnection
                        : Qt::BlockingQueuedConnection;
                const bool invoked = QMetaObject::invokeMethod(
                    qApp,
                    [&window, width, height, requestResize, requestSnapshot, snapshotPath, viewportOnly, &result]()
                    {
                        if (requestResize)
                        {
                            const int targetWidth = width > 0 ? width : window.width();
                            const int targetHeight = height > 0 ? height : window.height();
                            window.resize(targetWidth, targetHeight);
                        }
                        result = window.uiDebugJson();
                        if (requestSnapshot)
                        {
                            QWidget* target = viewportOnly
                                ? static_cast<QWidget*>(window.viewportHost())
                                : static_cast<QWidget*>(&window);
                            if (!target)
                            {
                                result.insert(QStringLiteral("snapshotSaved"), false);
                                result.insert(QStringLiteral("snapshotError"), QStringLiteral("snapshot target unavailable"));
                            }
                            else
                            {
                                const QPixmap pixmap = target->grab();
                                const bool saved = pixmap.save(snapshotPath, "PNG");
                                result.insert(QStringLiteral("snapshotSaved"), saved);
                                result.insert(QStringLiteral("snapshotPath"), snapshotPath);
                                result.insert(QStringLiteral("snapshotViewportOnly"), viewportOnly);
                                result.insert(QStringLiteral("snapshotWidth"), pixmap.width());
                                result.insert(QStringLiteral("snapshotHeight"), pixmap.height());
                                if (!saved)
                                {
                                    result.insert(QStringLiteral("snapshotError"),
                                                  QStringLiteral("failed to save snapshot to %1").arg(snapshotPath));
                                }
                            }
                        }
                    },
                    invokeType);

                if (!invoked)
                {
                    result.insert(QStringLiteral("error"), QStringLiteral("failed to query window state"));
                    return false;
                }
                result.insert(QStringLiteral("requestedResize"), requestResize);
                return true;
            }

            bool handledByService = false;
            const bool serviceOk = controlCommandService.tryHandle(command, body, result, handledByService);
            if (handledByService)
            {
                return serviceOk;
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
