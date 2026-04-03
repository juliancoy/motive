#pragma once

#include <QObject>
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
        QVector3D cameraPosition;
        QVector3D cameraRotation;
        int sceneItemCount = 0;
    };

    explicit EngineUiControlServer(std::function<QString()> rootPathProvider,
                                   std::function<ProfileData()> profileDataProvider,
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
    std::atomic<bool> m_running{false};
    int m_serverFd = -1;
    quint16 m_port = 0;
    std::unique_ptr<std::thread> m_thread;
};

}  // namespace motive::ui
