#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector3D>

namespace motive::ui {

class ProjectSession
{
public:
    ProjectSession();
    ~ProjectSession();

    QString currentProjectId() const;
    QString currentProjectRoot() const;
    QString currentGalleryPath() const;
    QString currentSelectedAssetPath() const;
    QString currentViewportAssetPath() const;
    QJsonArray currentSceneItems() const;
    QVector3D currentCameraPosition() const;
    QVector3D currentCameraRotation() const;
    QString currentRenderPath() const;
    QString projectsDirPath() const;
    QString currentProjectMarkerPath() const;
    QString projectPath(const QString& projectId) const;
    QString projectFilePathForProject(const QString& projectId) const;

    QString sanitizedProjectId(const QString& name) const;
    QStringList availableProjectIds() const;
    void ensureProjectsDirectory() const;
    void ensureDefaultProjectExists();

    bool switchToProject(const QString& projectId);
    bool createProject(const QString& name, const QString& rootPath = QString());
    void setCurrentProjectRoot(const QString& rootPath);
    void setCurrentGalleryPath(const QString& path);
    void setCurrentSelectedAssetPath(const QString& path);
    void setCurrentViewportAssetPath(const QString& path);
    void setCurrentSceneItems(const QJsonArray& items);
    void setCurrentCameraPosition(const QVector3D& position);
    void setCurrentCameraRotation(const QVector3D& rotation);
    void setCurrentRenderPath(const QString& renderPath);
    void saveCurrentProject() const;

private:
    QString legacyStateFilePathForProject(const QString& projectId) const;
    bool loadProject(const QString& projectId);
    void saveCurrentProjectMarker() const;
    QJsonObject buildBaseStateObject() const;
    QJsonObject buildProjectDocument(const QJsonObject& existingRoot = QJsonObject{}) const;
    QJsonObject currentStateFromDocument(const QString& projectId, const QJsonObject& root) const;
    void applyStateObject(const QString& projectId, const QJsonObject& state);

    QString m_currentProjectId;
    QString m_currentProjectRoot;
    QString m_currentGalleryPath;
    QString m_currentSelectedAssetPath;
    QString m_currentViewportAssetPath;
    QJsonArray m_currentSceneItems;
    QVector3D m_currentCameraPosition;
    QVector3D m_currentCameraRotation;
    QString m_currentRenderPath = QStringLiteral("forward3d");
};

}  // namespace motive::ui
