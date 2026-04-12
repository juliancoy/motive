#include "control_server.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QStringList>

#include <cerrno>
#include <cstring>

#include <arpa/inet.h>
#include <signal.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace motive::ui {

namespace {

QString reasonPhrase(int statusCode)
{
    switch (statusCode)
    {
    case 200: return QStringLiteral("OK");
    case 404: return QStringLiteral("Not Found");
    case 405: return QStringLiteral("Method Not Allowed");
    case 500: return QStringLiteral("Internal Server Error");
    default: return QStringLiteral("Status");
    }
}

QByteArray compactJson(const QJsonObject& object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QByteArray internalErrorBody(const QString& error)
{
    return compactJson(QJsonObject{
        {QStringLiteral("ok"), false},
        {QStringLiteral("error"), error}
    });
}

QString invokeRootPathProvider(const std::function<QString()>& provider)
{
    QString rootPath;
    if (!provider)
    {
        return QDir::currentPath();
    }

    const bool invoked = QMetaObject::invokeMethod(
        qApp,
        [&rootPath, &provider]()
        {
            rootPath = provider();
        },
        Qt::BlockingQueuedConnection);

    if (!invoked || rootPath.isEmpty())
    {
        return QDir::currentPath();
    }
    return rootPath;
}

EngineUiControlServer::ProfileData invokeProfileDataProvider(const std::function<EngineUiControlServer::ProfileData()>& provider)
{
    EngineUiControlServer::ProfileData data;
    if (!provider)
    {
        return data;
    }

    const bool invoked = QMetaObject::invokeMethod(
        qApp,
        [&data, &provider]()
        {
            data = provider();
        },
        Qt::BlockingQueuedConnection);

    return data;
}

QJsonObject buildDirectoryListing(const QString& rootPath)
{
    const QDir rootDir(rootPath.isEmpty() ? QDir::currentPath() : rootPath);
    const QString absoluteRoot = rootDir.absolutePath();
    QJsonArray entries;

    const QFileInfoList fileInfos = rootDir.entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot,
        QDir::DirsFirst | QDir::IgnoreCase | QDir::Name);

    for (const QFileInfo& info : fileInfos)
    {
        entries.append(QJsonObject{
            {QStringLiteral("name"), info.fileName()},
            {QStringLiteral("path"), info.absoluteFilePath()},
            {QStringLiteral("is_dir"), info.isDir()},
            {QStringLiteral("size"), static_cast<qint64>(info.size())}
        });
    }

    return QJsonObject{
        {QStringLiteral("ok"), true},
        {QStringLiteral("root"), absoluteRoot},
        {QStringLiteral("count"), entries.size()},
        {QStringLiteral("entries"), entries}
    };
}

}  // namespace

EngineUiControlServer::EngineUiControlServer(std::function<QString()> rootPathProvider,
                                             std::function<ProfileData()> profileDataProvider,
                                             std::function<bool(const QString&, const QJsonObject&, QJsonObject&)> commandHandler,
                                             QObject* parent)
    : QObject(parent),
      m_rootPathProvider(std::move(rootPathProvider)),
      m_profileDataProvider(std::move(profileDataProvider)),
      m_commandHandler(std::move(commandHandler))
{
}

EngineUiControlServer::~EngineUiControlServer()
{
    stop();
}

bool EngineUiControlServer::start(quint16 port)
{
    if (m_running.load())
    {
        qInfo() << "[EngineUiControlServer] start() ignored; already running on port" << m_port;
        return false;
    }

    m_serverFd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (m_serverFd < 0)
    {
        qWarning() << "[EngineUiControlServer] socket() failed:" << strerror(errno);
        return false;
    }

    int reuse = 1;
    ::setsockopt(m_serverFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#ifdef SO_NOSIGPIPE
    int noSigPipe = 1;
    ::setsockopt(m_serverFd, SOL_SOCKET, SO_NOSIGPIPE, &noSigPipe, sizeof(noSigPipe));
#endif
    ::signal(SIGPIPE, SIG_IGN);

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (::bind(m_serverFd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
    {
        qWarning() << "[EngineUiControlServer] bind() failed on port" << port << ":" << strerror(errno);
        ::close(m_serverFd);
        m_serverFd = -1;
        return false;
    }

    if (::listen(m_serverFd, 8) != 0)
    {
        qWarning() << "[EngineUiControlServer] listen() failed on port" << port << ":" << strerror(errno);
        ::close(m_serverFd);
        m_serverFd = -1;
        return false;
    }

    m_port = port;
    m_running.store(true);
    m_thread = std::make_unique<std::thread>(&EngineUiControlServer::run, this);
    qInfo() << "[EngineUiControlServer] Initialized on http://127.0.0.1:" << port;
    return true;
}

void EngineUiControlServer::stop()
{
    if (!m_running.exchange(false))
    {
        return;
    }

    if (m_serverFd >= 0)
    {
        ::shutdown(m_serverFd, SHUT_RDWR);
        ::close(m_serverFd);
        m_serverFd = -1;
    }

    if (m_thread && m_thread->joinable())
    {
        m_thread->join();
    }
    m_thread.reset();
}

void EngineUiControlServer::run()
{
    while (m_running.load())
    {
        const int clientFd = ::accept(m_serverFd, nullptr, nullptr);
        if (clientFd < 0)
        {
            if (m_running.load())
            {
                qWarning() << "[EngineUiControlServer] accept() failed:" << strerror(errno);
            }
            continue;
        }

        handleClient(clientFd);
        ::close(clientFd);
    }
}

void EngineUiControlServer::handleClient(int clientFd) const
{
    try
    {
        QByteArray request;
        request.reserve(8192);
        char buffer[8192];
        for (;;)
        {
            const ssize_t bytesRead = ::recv(clientFd, buffer, sizeof(buffer), 0);
            if (bytesRead <= 0)
            {
                break;
            }
            request.append(buffer, static_cast<int>(bytesRead));
            if (request.contains("\r\n\r\n") || request.size() >= 64 * 1024)
            {
                break;
            }
        }

        if (request.isEmpty())
        {
            return;
        }

        const QByteArray response = buildResponse(request);
        if (response.isEmpty())
        {
            return;
        }

        qint64 totalSent = 0;
        while (totalSent < response.size())
        {
            const ssize_t bytesSent = ::send(clientFd,
                                             response.constData() + totalSent,
                                             static_cast<size_t>(response.size() - totalSent),
#ifdef MSG_NOSIGNAL
                                             MSG_NOSIGNAL
#else
                                             0
#endif
            );
            if (bytesSent <= 0)
            {
                break;
            }
            totalSent += bytesSent;
        }
    }
    catch (const std::exception& ex)
    {
        const QByteArray response = jsonResponse(500, internalErrorBody(QStringLiteral("control server exception: %1").arg(QString::fromUtf8(ex.what()))));
        ::send(clientFd, response.constData(), static_cast<size_t>(response.size()),
#ifdef MSG_NOSIGNAL
               MSG_NOSIGNAL
#else
               0
#endif
        );
    }
    catch (...)
    {
        const QByteArray response = jsonResponse(500, internalErrorBody(QStringLiteral("control server exception")));
        ::send(clientFd, response.constData(), static_cast<size_t>(response.size()),
#ifdef MSG_NOSIGNAL
               MSG_NOSIGNAL
#else
               0
#endif
        );
    }
}

QByteArray EngineUiControlServer::buildResponse(const QByteArray& request) const
{
    try
    {
    const QList<QByteArray> lines = request.split('\n');
    if (lines.isEmpty())
    {
        return jsonResponse(500, internalErrorBody(QStringLiteral("invalid request")));
    }

    const QList<QByteArray> requestLine = lines.first().trimmed().split(' ');
    if (requestLine.size() < 2)
    {
        return jsonResponse(500, internalErrorBody(QStringLiteral("invalid request line")));
    }

    const QByteArray method = requestLine.at(0);
    const QByteArray path = requestLine.at(1);

    if (method == "POST")
    {
        const int headerEnd = request.indexOf("\r\n\r\n");
        const QByteArray bodyBytes = headerEnd >= 0 ? request.mid(headerEnd + 4) : QByteArray{};
        const QJsonDocument bodyDoc = QJsonDocument::fromJson(bodyBytes);
        const QJsonObject body = bodyDoc.isObject() ? bodyDoc.object() : QJsonObject{};
        if (path == "/controls/primitive")
        {
            QJsonObject result;
            if (!m_commandHandler || !m_commandHandler(QStringLiteral("primitive"), body, result))
            {
                return jsonResponse(500, compactJson(QJsonObject{
                    {QStringLiteral("ok"), false},
                    {QStringLiteral("error"), QStringLiteral("primitive control failed")}
                }));
            }
            result.insert(QStringLiteral("ok"), true);
            return jsonResponse(200, compactJson(result));
        }
        if (path == "/controls/scene-item")
        {
            QJsonObject result;
            if (!m_commandHandler || !m_commandHandler(QStringLiteral("scene_item"), body, result))
            {
                return jsonResponse(500, compactJson(QJsonObject{
                    {QStringLiteral("ok"), false},
                    {QStringLiteral("error"), QStringLiteral("scene-item control failed")}
                }));
            }
            result.insert(QStringLiteral("ok"), true);
            return jsonResponse(200, compactJson(result));
        }
        if (path == "/controls/character")
        {
            QJsonObject result;
            if (!m_commandHandler || !m_commandHandler(QStringLiteral("character"), body, result))
            {
                return jsonResponse(500, compactJson(QJsonObject{
                    {QStringLiteral("ok"), false},
                    {QStringLiteral("error"), QStringLiteral("character control failed")}
                }));
            }
            result.insert(QStringLiteral("ok"), true);
            return jsonResponse(200, compactJson(result));
        }
        if (path == "/controls/camera")
        {
            QJsonObject result;
            if (!m_commandHandler || !m_commandHandler(QStringLiteral("camera"), body, result))
            {
                return jsonResponse(500, compactJson(QJsonObject{
                    {QStringLiteral("ok"), false},
                    {QStringLiteral("error"), QStringLiteral("camera control failed")}
                }));
            }
            result.insert(QStringLiteral("ok"), true);
            return jsonResponse(200, compactJson(result));
        }
        if (path == "/controls/rebuild")
        {
            QJsonObject result;
            if (!m_commandHandler || !m_commandHandler(QStringLiteral("rebuild"), body, result))
            {
                return jsonResponse(500, compactJson(QJsonObject{
                    {QStringLiteral("ok"), false},
                    {QStringLiteral("error"), QStringLiteral("rebuild failed")}
                }));
            }
            result.insert(QStringLiteral("ok"), true);
            return jsonResponse(200, compactJson(result));
        }
        if (path == "/controls/reset")
        {
            QJsonObject result;
            if (!m_commandHandler || !m_commandHandler(QStringLiteral("reset"), body, result))
            {
                return jsonResponse(500, compactJson(QJsonObject{
                    {QStringLiteral("ok"), false},
                    {QStringLiteral("error"), QStringLiteral("reset failed")}
                }));
            }
            result.insert(QStringLiteral("ok"), true);
            return jsonResponse(200, compactJson(result));
        }
        if (path == "/controls/physics_coupling")
        {
            QJsonObject result;
            if (!m_commandHandler || !m_commandHandler(QStringLiteral("physics_coupling"), body, result))
            {
                return jsonResponse(500, compactJson(QJsonObject{
                    {QStringLiteral("ok"), false},
                    {QStringLiteral("error"), QStringLiteral("physics_coupling control failed")}
                }));
            }
            result.insert(QStringLiteral("ok"), true);
            return jsonResponse(200, compactJson(result));
        }
        if (path == "/controls/physics_gravity")
        {
            QJsonObject result;
            if (!m_commandHandler || !m_commandHandler(QStringLiteral("physics_gravity"), body, result))
            {
                return jsonResponse(500, compactJson(QJsonObject{
                    {QStringLiteral("ok"), false},
                    {QStringLiteral("error"), QStringLiteral("physics_gravity control failed")}
                }));
            }
            result.insert(QStringLiteral("ok"), true);
            return jsonResponse(200, compactJson(result));
        }
        if (path == "/controls/window")
        {
            QJsonObject result;
            if (!m_commandHandler || !m_commandHandler(QStringLiteral("window"), body, result))
            {
                return jsonResponse(500, compactJson(QJsonObject{
                    {QStringLiteral("ok"), false},
                    {QStringLiteral("error"), QStringLiteral("window control failed")}
                }));
            }
            result.insert(QStringLiteral("ok"), true);
            return jsonResponse(200, compactJson(result));
        }

        return jsonResponse(404, compactJson(QJsonObject{
            {QStringLiteral("ok"), false},
            {QStringLiteral("error"), QStringLiteral("not found")}
        }));
    }

    if (method != "GET")
    {
        return jsonResponse(405, compactJson(QJsonObject{
            {QStringLiteral("ok"), false},
            {QStringLiteral("error"), QStringLiteral("method not allowed")}
        }));
    }

    if (path == "/health")
    {
        return jsonResponse(200, compactJson(QJsonObject{
            {QStringLiteral("ok"), true},
            {QStringLiteral("port"), static_cast<int>(m_port)}
        }));
    }

    if (path == "/ls" || path == "/root-ls")
    {
        const QString rootPath = invokeRootPathProvider(m_rootPathProvider);
        return jsonResponse(200, compactJson(buildDirectoryListing(rootPath)));
    }

    if (path == "/profile/status")
    {
        return jsonResponse(200, compactJson(QJsonObject{
            {QStringLiteral("ok"), true},
            {QStringLiteral("application"), QStringLiteral("MotiveEditor")},
            {QStringLiteral("port"), static_cast<int>(m_port)}
        }));
    }

    if (path == "/profile/scene")
    {
        const EngineUiControlServer::ProfileData data = invokeProfileDataProvider(m_profileDataProvider);
        QJsonArray cameraPosArray;
        cameraPosArray.append(data.cameraPosition.x());
        cameraPosArray.append(data.cameraPosition.y());
        cameraPosArray.append(data.cameraPosition.z());
        
        QJsonArray cameraRotArray;
        cameraRotArray.append(data.cameraRotation.x());
        cameraRotArray.append(data.cameraRotation.y());
        cameraRotArray.append(data.cameraRotation.z());
        
        QJsonArray sceneItemsArray;
        for (const auto& sceneItem : data.sceneItems)
        {
            sceneItemsArray.append(sceneItem);
        }

        QJsonObject payload;
        payload.insert(QStringLiteral("ok"), true);
        payload.insert(QStringLiteral("rootPath"), data.rootPath);
        payload.insert(QStringLiteral("sceneItemCount"), data.sceneItemCount);
        payload.insert(QStringLiteral("sceneItems"), sceneItemsArray);
        payload.insert(QStringLiteral("hierarchy"), data.hierarchy);
        payload.insert(QStringLiteral("cameraPosition"), cameraPosArray);
        payload.insert(QStringLiteral("cameraRotation"), cameraRotArray);
        // Performance metrics
        payload.insert(QStringLiteral("fps"), static_cast<double>(data.currentFps));
        payload.insert(QStringLiteral("renderIntervalMs"), data.renderIntervalMs);
        payload.insert(QStringLiteral("renderTimerActive"), data.renderTimerActive);
        payload.insert(QStringLiteral("viewportWidth"), data.viewportWidth);
        payload.insert(QStringLiteral("viewportHeight"), data.viewportHeight);
        return jsonResponse(200, compactJson(payload));
    }

    if (path == "/profile/window")
    {
        const EngineUiControlServer::ProfileData data = invokeProfileDataProvider(m_profileDataProvider);
        QJsonObject payload;
        payload.insert(QStringLiteral("ok"), true);
        payload.insert(QStringLiteral("window"), data.uiDebug.value(QStringLiteral("window")).toObject());
        payload.insert(QStringLiteral("splitters"), data.uiDebug.value(QStringLiteral("splitters")).toArray());
        payload.insert(QStringLiteral("dockWidgets"), data.uiDebug.value(QStringLiteral("dockWidgets")).toArray());
        return jsonResponse(200, compactJson(payload));
    }

    if (path == "/profile/ui")
    {
        const EngineUiControlServer::ProfileData data = invokeProfileDataProvider(m_profileDataProvider);
        QJsonObject payload = data.uiDebug;
        payload.insert(QStringLiteral("ok"), true);
        return jsonResponse(200, compactJson(payload));
    }

    if (path == "/profile/performance")
    {
        const EngineUiControlServer::ProfileData data = invokeProfileDataProvider(m_profileDataProvider);
        QJsonObject payload;
        payload.insert(QStringLiteral("ok"), true);
        payload.insert(QStringLiteral("fps"), static_cast<double>(data.currentFps));
        payload.insert(QStringLiteral("renderIntervalMs"), data.renderIntervalMs);
        payload.insert(QStringLiteral("renderTimerActive"), data.renderTimerActive);
        payload.insert(QStringLiteral("targetFps"), data.renderIntervalMs > 0 ? (1000 / data.renderIntervalMs) : 0);
        payload.insert(QStringLiteral("viewportWidth"), data.viewportWidth);
        payload.insert(QStringLiteral("viewportHeight"), data.viewportHeight);
        return jsonResponse(200, compactJson(payload));
    }

    if (path == "/hierarchy")
    {
        const EngineUiControlServer::ProfileData data = invokeProfileDataProvider(m_profileDataProvider);
        QJsonObject payload;
        payload.insert(QStringLiteral("ok"), true);
        payload.insert(QStringLiteral("rootPath"), data.rootPath);
        payload.insert(QStringLiteral("hierarchy"), data.hierarchy);
        return jsonResponse(200, compactJson(payload));
    }

    if (path == "/controls/camera")
    {
        QJsonObject result;
        if (!m_commandHandler || !m_commandHandler(QStringLiteral("camera"), QJsonObject{}, result))
        {
            return jsonResponse(500, compactJson(QJsonObject{
                {QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("camera control failed")}
            }));
        }
        result.insert(QStringLiteral("ok"), true);
        return jsonResponse(200, compactJson(result));
    }

    if (path == "/controls/character")
    {
        QJsonObject result;
        if (!m_commandHandler || !m_commandHandler(QStringLiteral("character"), QJsonObject{}, result))
        {
            return jsonResponse(500, compactJson(QJsonObject{
                {QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("character control failed")}
            }));
        }
        result.insert(QStringLiteral("ok"), true);
        return jsonResponse(200, compactJson(result));
    }

    return jsonResponse(404, compactJson(QJsonObject{
        {QStringLiteral("ok"), false},
        {QStringLiteral("error"), QStringLiteral("not found")}
    }));
    }
    catch (const std::exception& ex)
    {
        return jsonResponse(500, internalErrorBody(QStringLiteral("buildResponse exception: %1").arg(QString::fromUtf8(ex.what()))));
    }
    catch (...)
    {
        return jsonResponse(500, internalErrorBody(QStringLiteral("buildResponse exception")));
    }
}

QByteArray EngineUiControlServer::jsonResponse(int statusCode, const QByteArray& body) const
{
    QByteArray response;
    response += "HTTP/1.1 " + QByteArray::number(statusCode) + " " + reasonPhrase(statusCode).toUtf8() + "\r\n";
    response += "Content-Type: application/json\r\n";
    response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    response += "Connection: close\r\n\r\n";
    response += body;
    return response;
}

}  // namespace motive::ui
