#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector3D>

#include <condition_variable>
#include <mutex>
#include <thread>

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
    QString currentPhysicsEngine() const;
    QVector3D currentWorldGravity() const;
    int currentPhysicsMaxSubSteps() const;
    bool currentPhysicsDebugDraw() const;
    bool currentPhysicsAutoSync() const;
    bool currentMeshConsolidationEnabled() const;
    bool currentValidationLayersEnabled() const;
    bool currentFreeFlyCameraEnabled() const;
    float currentMinPreviewTriangleAreaPx() const;
    int currentViewportCount() const;
    QJsonArray currentViewportCameraIds() const;
    QJsonObject currentUiState() const;
    QString configFilePath() const;
    QString rootDirPath() const;
    void setRootDirPath(const QString& path);
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
    void setCurrentPhysicsEngine(const QString& engineName);
    void setCurrentWorldGravity(const QVector3D& gravity);
    void setCurrentPhysicsMaxSubSteps(int maxSubSteps);
    void setCurrentPhysicsDebugDraw(bool enabled);
    void setCurrentPhysicsAutoSync(bool enabled);
    void setCurrentMeshConsolidationEnabled(bool enabled);
    void setCurrentValidationLayersEnabled(bool enabled);
    void setCurrentFreeFlyCameraEnabled(bool enabled);
    void setCurrentMinPreviewTriangleAreaPx(float areaPx);
    void setCurrentViewportCount(int count);
    void setCurrentViewportCameraIds(const QJsonArray& ids);
    void setCurrentUiState(const QJsonObject& state);
    void saveCurrentProject() const;
    void requestSaveCurrentProject() const;
    void flushPendingSave() const;

private:
    struct SaveRequest
    {
        QString projectId;
        QString projectDirPath;
        QString projectFilePath;
        QString legacyStateFilePath;
        QJsonObject state;
    };

    QString legacyStateFilePathForProject(const QString& projectId) const;
    bool loadProject(const QString& projectId);
    void saveCurrentProjectMarker() const;
    QJsonObject buildBaseStateObject() const;
    QJsonObject buildProjectDocument(const QJsonObject& existingRoot = QJsonObject{}) const;
    QJsonObject buildProjectDocumentForState(const QString& projectId,
                                             const QJsonObject& state,
                                             const QJsonObject& existingRoot = QJsonObject{}) const;
    SaveRequest buildSaveRequest() const;
    void writeSaveRequest(const SaveRequest& request) const;
    void ensureSaveWorkerStarted() const;
    void saveWorkerLoop() const;
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
    QString m_currentPhysicsEngine = QStringLiteral("Bullet");
    QVector3D m_currentWorldGravity = QVector3D(0.0f, -9.81f, 0.0f);
    int m_currentPhysicsMaxSubSteps = 1;
    bool m_currentPhysicsDebugDraw = false;
    bool m_currentPhysicsAutoSync = true;
    bool m_currentMeshConsolidationEnabled = true;
    bool m_currentValidationLayersEnabled = true;
    bool m_currentFreeFlyCameraEnabled = true;  // Default to free fly mode
    float m_currentMinPreviewTriangleAreaPx = 0.25f;
    int m_currentViewportCount = 1;
    QJsonArray m_currentViewportCameraIds;
    QJsonObject m_currentUiState;

    mutable std::mutex m_saveMutex;
    mutable std::condition_variable m_saveCv;
    mutable std::condition_variable m_saveIdleCv;
    mutable std::thread m_saveWorker;
    mutable SaveRequest m_pendingSave;
    mutable uint64_t m_saveGeneration = 0;
    mutable bool m_hasPendingSave = false;
    mutable bool m_saveInProgress = false;
    mutable bool m_stopSaveWorker = false;
};

}  // namespace motive::ui
