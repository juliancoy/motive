#pragma once

#include <QObject>
#include <QJsonArray>
#include <QString>
#include <QVector3D>
#include <QJsonObject>
#include <QList>

#include <atomic>
#include <functional>
#include <memory>
#include <thread>

namespace motive::ui {

class EngineUiControlServer : public QObject
{
public:
    struct ProfileData
    {
        QString rootPath;
        QList<QJsonObject> sceneItems;
        QJsonArray hierarchy;
        QJsonObject uiDebug;
        QVector3D cameraPosition;
        QVector3D cameraRotation;
        int sceneItemCount = 0;
        // Performance metrics
        float currentFps = 0.0f;
        int renderIntervalMs = 16;
        bool renderTimerActive = false;
        int viewportWidth = 0;
        int viewportHeight = 0;
    };
    
    struct PerformanceMetrics
    {
        float fps = 0.0f;
        float frameTimeMs = 0.0f;
        int renderIntervalMs = 16;
        bool vsyncEnabled = false;
        int triangleCount = 0;
        int drawCallCount = 0;
    };

    explicit EngineUiControlServer(std::function<QString()> rootPathProvider,
                                   std::function<ProfileData()> profileDataProvider,
                                   std::function<bool(const QString&, const QJsonObject&, QJsonObject&)> commandHandler = {},
                                   QObject* parent = nullptr);
    ~EngineUiControlServer() override;

    bool start(quint16 port);
    void stop();

private:
    void run();
    void handleClient(int clientFd) const;
    QByteArray buildResponse(const QByteArray& request) const;
    QByteArray jsonResponse(int statusCode, const QByteArray& body) const;

    std::function<QString()> m_rootPathProvider;
    std::function<ProfileData()> m_profileDataProvider;
    std::function<bool(const QString&, const QJsonObject&, QJsonObject&)> m_commandHandler;
    std::atomic<bool> m_running{false};
    int m_serverFd = -1;
    quint16 m_port = 0;
    std::unique_ptr<std::thread> m_thread;
};

}  // namespace motive::ui
