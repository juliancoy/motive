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
    QJsonArray currentCameraConfigs() const;
    QVector3D currentCameraPosition() const;
    QVector3D currentCameraRotation() const;
    float currentCameraSpeed() const;
    QJsonObject currentSceneLight() const;
    QString currentRenderPath() const;
    bool currentMeshConsolidationEnabled() const;
    bool currentValidationLayersEnabled() const;
    bool currentFreeFlyCameraEnabled() const;
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
    void setCurrentCameraConfigs(const QJsonArray& configs);
    void setCurrentCameraPosition(const QVector3D& position);
    void setCurrentCameraRotation(const QVector3D& rotation);
    void setCurrentCameraSpeed(float speed);
    void setCurrentSceneLight(const QJsonObject& light);
    void setCurrentRenderPath(const QString& renderPath);
    void setCurrentMeshConsolidationEnabled(bool enabled);
    void setCurrentValidationLayersEnabled(bool enabled);
    void setCurrentFreeFlyCameraEnabled(bool enabled);
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
    QJsonArray m_currentCameraConfigs;
    QVector3D m_currentCameraPosition;
    QVector3D m_currentCameraRotation;
    float m_currentCameraSpeed = 0.01f;
    QJsonObject m_currentSceneLight;
    QString m_currentRenderPath = QStringLiteral("forward3d");
    bool m_currentMeshConsolidationEnabled = true;
    bool m_currentValidationLayersEnabled = true;
    bool m_currentFreeFlyCameraEnabled = true;  // Default to free fly mode
};

}  // namespace motive::ui
