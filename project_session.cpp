#include "project_session.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

#include <algorithm>

namespace motive::ui {

namespace {

QString defaultProjectId()
{
    return QStringLiteral("default");
}

}

ProjectSession::ProjectSession()
{
    ensureDefaultProjectExists();
    QString targetProjectId = defaultProjectId();
    QFile markerFile(currentProjectMarkerPath());
    if (markerFile.open(QIODevice::ReadOnly))
    {
        const QString markedId = QString::fromUtf8(markerFile.readAll()).trimmed();
        if (!markedId.isEmpty())
        {
            targetProjectId = sanitizedProjectId(markedId);
        }
    }

    if (!switchToProject(targetProjectId))
    {
        m_currentProjectId = defaultProjectId();
        m_currentProjectRoot = QDir::currentPath();
        saveCurrentProject();
        saveCurrentProjectMarker();
    }
}

ProjectSession::~ProjectSession() = default;

QString ProjectSession::currentProjectId() const
{
    return m_currentProjectId;
}

QString ProjectSession::currentProjectRoot() const
{
    return m_currentProjectRoot;
}

QString ProjectSession::currentGalleryPath() const
{
    return m_currentGalleryPath;
}

QString ProjectSession::currentSelectedAssetPath() const
{
    return m_currentSelectedAssetPath;
}

QString ProjectSession::currentViewportAssetPath() const
{
    return m_currentViewportAssetPath;
}

QJsonArray ProjectSession::currentSceneItems() const
{
    return m_currentSceneItems;
}

QJsonArray ProjectSession::currentCameraConfigs() const
{
    return m_currentCameraConfigs;
}

QVector3D ProjectSession::currentCameraPosition() const
{
    return m_currentCameraPosition;
}

QVector3D ProjectSession::currentCameraRotation() const
{
    return m_currentCameraRotation;
}

float ProjectSession::currentCameraSpeed() const
{
    return m_currentCameraSpeed;
}

QJsonObject ProjectSession::currentSceneLight() const
{
    return m_currentSceneLight;
}

QString ProjectSession::currentRenderPath() const
{
    return m_currentRenderPath;
}

bool ProjectSession::currentMeshConsolidationEnabled() const
{
    return m_currentMeshConsolidationEnabled;
}

bool ProjectSession::currentValidationLayersEnabled() const
{
    return m_currentValidationLayersEnabled;
}

bool ProjectSession::currentFreeFlyCameraEnabled() const
{
    return m_currentFreeFlyCameraEnabled;
}

int ProjectSession::currentViewportCount() const
{
    return m_currentViewportCount;
}

QJsonArray ProjectSession::currentViewportCameraIds() const
{
    return m_currentViewportCameraIds;
}

QString ProjectSession::projectsDirPath() const
{
    return QDir(QDir::currentPath()).filePath(QStringLiteral("projects"));
}

QString ProjectSession::currentProjectMarkerPath() const
{
    return QDir(projectsDirPath()).filePath(QStringLiteral(".current_project"));
}

QString ProjectSession::projectPath(const QString& projectId) const
{
    return QDir(projectsDirPath()).filePath(projectId);
}

QString ProjectSession::projectFilePathForProject(const QString& projectId) const
{
    return QDir(projectPath(projectId)).filePath(projectId + QStringLiteral(".json"));
}

QString ProjectSession::sanitizedProjectId(const QString& name) const
{
    QString normalized = name.trimmed().toLower();
    normalized.replace(QRegularExpression(QStringLiteral("[^a-z0-9._-]+")), QStringLiteral("-"));
    normalized.replace(QRegularExpression(QStringLiteral("-{2,}")), QStringLiteral("-"));
    normalized.remove(QRegularExpression(QStringLiteral("^-+|-+$")));
    return normalized.isEmpty() ? defaultProjectId() : normalized;
}

QStringList ProjectSession::availableProjectIds() const
{
    QDir dir(projectsDirPath());
    if (!dir.exists())
    {
        return {};
    }

    QStringList ids;
    const QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo& entry : entries)
    {
        ids.push_back(entry.fileName());
    }
    return ids;
}

void ProjectSession::ensureProjectsDirectory() const
{
    QDir().mkpath(projectsDirPath());
}

void ProjectSession::ensureDefaultProjectExists()
{
    ensureProjectsDirectory();
    const QString id = defaultProjectId();
    QDir().mkpath(projectPath(id));
    const QString statePath = projectFilePathForProject(id);
    if (!QFileInfo::exists(statePath))
    {
        const QString previousId = m_currentProjectId;
        const QString previousRoot = m_currentProjectRoot;
        m_currentProjectId = id;
        m_currentProjectRoot = QDir::currentPath();
        saveCurrentProject();
        m_currentProjectId = previousId;
        m_currentProjectRoot = previousRoot;
    }
}

bool ProjectSession::switchToProject(const QString& projectId)
{
    const QString id = sanitizedProjectId(projectId);
    ensureProjectsDirectory();
    if (!QFileInfo::exists(projectPath(id)))
    {
        return false;
    }
    if (!loadProject(id))
    {
        return false;
    }
    saveCurrentProjectMarker();
    return true;
}

bool ProjectSession::createProject(const QString& name, const QString& rootPath)
{
    const QString id = sanitizedProjectId(name);
    ensureProjectsDirectory();
    const QString dirPath = projectPath(id);
    if (QFileInfo::exists(dirPath))
    {
        return false;
    }

    if (!QDir().mkpath(dirPath))
    {
        return false;
    }

    m_currentProjectId = id;
    m_currentProjectRoot = QFileInfo(rootPath).isDir() ? QFileInfo(rootPath).absoluteFilePath() : QDir::currentPath();
    saveCurrentProject();
    saveCurrentProjectMarker();
    return true;
}

void ProjectSession::setCurrentProjectRoot(const QString& rootPath)
{
    const QFileInfo info(rootPath);
    if (info.exists() && info.isDir())
    {
        m_currentProjectRoot = info.absoluteFilePath();
    }
}

void ProjectSession::setCurrentGalleryPath(const QString& path)
{
    m_currentGalleryPath = path;
}

void ProjectSession::setCurrentSelectedAssetPath(const QString& path)
{
    m_currentSelectedAssetPath = path;
}

void ProjectSession::setCurrentViewportAssetPath(const QString& path)
{
    m_currentViewportAssetPath = path;
}

void ProjectSession::setCurrentSceneItems(const QJsonArray& items)
{
    m_currentSceneItems = items;
}

void ProjectSession::setCurrentCameraConfigs(const QJsonArray& configs)
{
    m_currentCameraConfigs = configs;
}

void ProjectSession::setCurrentCameraPosition(const QVector3D& position)
{
    m_currentCameraPosition = position;
}

void ProjectSession::setCurrentCameraRotation(const QVector3D& rotation)
{
    m_currentCameraRotation = rotation;
}

void ProjectSession::setCurrentCameraSpeed(float speed)
{
    m_currentCameraSpeed = speed;
}

void ProjectSession::setCurrentSceneLight(const QJsonObject& light)
{
    m_currentSceneLight = light;
}

void ProjectSession::setCurrentRenderPath(const QString& renderPath)
{
    m_currentRenderPath = renderPath.trimmed().isEmpty() ? QStringLiteral("forward3d") : renderPath;
}

void ProjectSession::setCurrentMeshConsolidationEnabled(bool enabled)
{
    m_currentMeshConsolidationEnabled = enabled;
}

void ProjectSession::setCurrentValidationLayersEnabled(bool enabled)
{
    m_currentValidationLayersEnabled = enabled;
}

void ProjectSession::setCurrentFreeFlyCameraEnabled(bool enabled)
{
    m_currentFreeFlyCameraEnabled = enabled;
}

void ProjectSession::setCurrentViewportCount(int count)
{
    m_currentViewportCount = std::clamp(count, 1, 4);
}

void ProjectSession::setCurrentViewportCameraIds(const QJsonArray& ids)
{
    m_currentViewportCameraIds = ids;
}

void ProjectSession::saveCurrentProject() const
{
    if (m_currentProjectId.isEmpty())
    {
        return;
    }

    ensureProjectsDirectory();
    QDir().mkpath(projectPath(m_currentProjectId));

    const QString projectFilePath = projectFilePathForProject(m_currentProjectId);
    QJsonObject existingRoot;
    QFile existingFile(projectFilePath);
    if (existingFile.exists() && existingFile.open(QIODevice::ReadOnly))
    {
        const QJsonDocument existingDoc = QJsonDocument::fromJson(existingFile.readAll());
        if (existingDoc.isObject())
        {
            existingRoot = existingDoc.object();
        }
    }

    QFile file(projectFilePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        return;
    }

    file.write(QJsonDocument(buildProjectDocument(existingRoot)).toJson(QJsonDocument::Indented));
    QFile::remove(legacyStateFilePathForProject(m_currentProjectId));
}

bool ProjectSession::loadProject(const QString& projectId)
{
    QFile file(projectFilePathForProject(projectId));
    if (!file.exists())
    {
        QFile legacyFile(legacyStateFilePathForProject(projectId));
        if (legacyFile.exists() && legacyFile.rename(projectFilePathForProject(projectId)))
        {
            file.setFileName(projectFilePathForProject(projectId));
        }
    }
    if (!file.exists())
    {
        m_currentProjectId = projectId;
        m_currentProjectRoot = QDir::currentPath();
        saveCurrentProject();
        return true;
    }
    if (!file.open(QIODevice::ReadOnly))
    {
        return false;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
    {
        return false;
    }

    applyStateObject(projectId, currentStateFromDocument(projectId, doc.object()));
    if (QFileInfo::exists(projectFilePathForProject(projectId)))
    {
        QFile::remove(legacyStateFilePathForProject(projectId));
    }
    return true;
}

void ProjectSession::saveCurrentProjectMarker() const
{
    ensureProjectsDirectory();
    QFile existing(currentProjectMarkerPath());
    if (existing.open(QIODevice::ReadOnly))
    {
        const QByteArray current = existing.readAll().trimmed();
        if (current == m_currentProjectId.toUtf8())
        {
            return;
        }
    }

    QFile file(currentProjectMarkerPath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        return;
    }
    file.write(m_currentProjectId.toUtf8());
}

QString ProjectSession::legacyStateFilePathForProject(const QString& projectId) const
{
    return QDir(projectPath(projectId)).filePath(QStringLiteral("state.json"));
}

QJsonObject ProjectSession::buildBaseStateObject() const
{
    return QJsonObject{
        {QStringLiteral("projectRoot"), m_currentProjectRoot},
        {QStringLiteral("galleryPath"), m_currentGalleryPath},
        {QStringLiteral("selectedAssetPath"), m_currentSelectedAssetPath},
        {QStringLiteral("viewportAssetPath"), m_currentViewportAssetPath},
        {QStringLiteral("sceneItems"), m_currentSceneItems},
        {QStringLiteral("cameraConfigs"), m_currentCameraConfigs},
        {QStringLiteral("cameraPosition"), QJsonArray{m_currentCameraPosition.x(), m_currentCameraPosition.y(), m_currentCameraPosition.z()}},
        {QStringLiteral("cameraRotation"), QJsonArray{m_currentCameraRotation.x(), m_currentCameraRotation.y(), m_currentCameraRotation.z()}},
        {QStringLiteral("cameraSpeed"), m_currentCameraSpeed},
        {QStringLiteral("sceneLight"), m_currentSceneLight},
        {QStringLiteral("renderPath"), m_currentRenderPath},
        {QStringLiteral("meshConsolidationEnabled"), m_currentMeshConsolidationEnabled},
        {QStringLiteral("validationLayersEnabled"), m_currentValidationLayersEnabled},
        {QStringLiteral("freeFlyCameraEnabled"), m_currentFreeFlyCameraEnabled},
        {QStringLiteral("viewportCount"), m_currentViewportCount},
        {QStringLiteral("viewportCameraIds"), m_currentViewportCameraIds}
    };
}

QJsonObject ProjectSession::buildProjectDocument(const QJsonObject& existingRoot) const
{
    QJsonObject root = existingRoot;
    if (root.isEmpty())
    {
        root = QJsonObject{
            {QStringLiteral("projectId"), m_currentProjectId},
            {QStringLiteral("base"), buildBaseStateObject()},
            {QStringLiteral("changes"), QJsonArray{}}
        };
        return root;
    }

    root[QStringLiteral("projectId")] = m_currentProjectId;
    if (!root.contains(QStringLiteral("base")) || !root.value(QStringLiteral("base")).isObject())
    {
        root[QStringLiteral("base")] = QJsonObject{
            {QStringLiteral("projectRoot"), currentStateFromDocument(m_currentProjectId, root).value(QStringLiteral("projectRoot")).toString(QDir::currentPath())}
        };
    }

    QJsonObject currentState = currentStateFromDocument(m_currentProjectId, root);
    const QString currentStoredRoot = currentState.value(QStringLiteral("projectRoot")).toString();
    QJsonArray changes = root.value(QStringLiteral("changes")).toArray();

    const qint64 tsMs = QDateTime::currentMSecsSinceEpoch();
    const auto appendSetIfChanged = [&](const QString& field, const QJsonValue& value)
    {
        if (currentState.value(field) != value)
        {
            changes.append(QJsonObject{
                {QStringLiteral("op"), QStringLiteral("set")},
                {QStringLiteral("field"), field},
                {QStringLiteral("value"), value},
                {QStringLiteral("ts_ms"), tsMs}
            });
        }
    };

    appendSetIfChanged(QStringLiteral("projectRoot"), m_currentProjectRoot);
    appendSetIfChanged(QStringLiteral("galleryPath"), m_currentGalleryPath);
    appendSetIfChanged(QStringLiteral("selectedAssetPath"), m_currentSelectedAssetPath);
    appendSetIfChanged(QStringLiteral("viewportAssetPath"), m_currentViewportAssetPath);
    appendSetIfChanged(QStringLiteral("sceneItems"), m_currentSceneItems);
    appendSetIfChanged(QStringLiteral("cameraConfigs"), m_currentCameraConfigs);
    appendSetIfChanged(QStringLiteral("cameraPosition"), QJsonArray{m_currentCameraPosition.x(), m_currentCameraPosition.y(), m_currentCameraPosition.z()});
    appendSetIfChanged(QStringLiteral("cameraRotation"), QJsonArray{m_currentCameraRotation.x(), m_currentCameraRotation.y(), m_currentCameraRotation.z()});
    appendSetIfChanged(QStringLiteral("cameraSpeed"), m_currentCameraSpeed);
    appendSetIfChanged(QStringLiteral("sceneLight"), m_currentSceneLight);
    appendSetIfChanged(QStringLiteral("renderPath"), m_currentRenderPath);
    appendSetIfChanged(QStringLiteral("meshConsolidationEnabled"), m_currentMeshConsolidationEnabled);
    appendSetIfChanged(QStringLiteral("validationLayersEnabled"), m_currentValidationLayersEnabled);
    appendSetIfChanged(QStringLiteral("freeFlyCameraEnabled"), m_currentFreeFlyCameraEnabled);
    appendSetIfChanged(QStringLiteral("viewportCount"), m_currentViewportCount);
    appendSetIfChanged(QStringLiteral("viewportCameraIds"), m_currentViewportCameraIds);

    root[QStringLiteral("changes")] = changes;
    return root;
}

QJsonObject ProjectSession::currentStateFromDocument(const QString& projectId, const QJsonObject& root) const
{
    if (root.contains(QStringLiteral("base")) || root.contains(QStringLiteral("changes")))
    {
        QJsonObject state = root.value(QStringLiteral("base")).toObject();
        QJsonArray changes = root.value(QStringLiteral("changes")).toArray();
        for (const QJsonValue& value : changes)
        {
            if (!value.isObject())
            {
                continue;
            }
            const QJsonObject change = value.toObject();
            if (change.value(QStringLiteral("op")).toString() != QStringLiteral("set"))
            {
                continue;
            }
            const QString field = change.value(QStringLiteral("field")).toString();
            if (field.isEmpty())
            {
                continue;
            }
            state[field] = change.value(QStringLiteral("value"));
        }
        return state;
    }

    // Legacy flat format compatibility.
    return QJsonObject{
        {QStringLiteral("projectId"), root.value(QStringLiteral("projectId")).toString(projectId)},
        {QStringLiteral("projectRoot"), root.value(QStringLiteral("projectRoot")).toString(QDir::currentPath())},
        {QStringLiteral("galleryPath"), root.value(QStringLiteral("galleryPath")).toString()},
        {QStringLiteral("selectedAssetPath"), root.value(QStringLiteral("selectedAssetPath")).toString()},
        {QStringLiteral("viewportAssetPath"), root.value(QStringLiteral("viewportAssetPath")).toString()},
        {QStringLiteral("sceneItems"), root.value(QStringLiteral("sceneItems")).toArray()},
        {QStringLiteral("sceneAssetPaths"), root.value(QStringLiteral("sceneAssetPaths")).toArray()},
        {QStringLiteral("cameraConfigs"), root.value(QStringLiteral("cameraConfigs")).toArray()},
        {QStringLiteral("cameraPosition"), root.contains(QStringLiteral("cameraPosition")) ? root.value(QStringLiteral("cameraPosition")) : QJsonValue(QJsonArray{0.0, 0.0, 3.0})},
        {QStringLiteral("cameraRotation"), root.contains(QStringLiteral("cameraRotation")) ? root.value(QStringLiteral("cameraRotation")) : QJsonValue(QJsonArray{0.0, 0.0, 0.0})},
        {QStringLiteral("cameraSpeed"), root.value(QStringLiteral("cameraSpeed")).toDouble(0.01)},
        {QStringLiteral("renderPath"), root.value(QStringLiteral("renderPath")).toString(QStringLiteral("forward3d"))},
        {QStringLiteral("meshConsolidationEnabled"), root.contains(QStringLiteral("meshConsolidationEnabled")) ? root.value(QStringLiteral("meshConsolidationEnabled")).toBool(true) : true},
        {QStringLiteral("validationLayersEnabled"), root.contains(QStringLiteral("validationLayersEnabled")) ? root.value(QStringLiteral("validationLayersEnabled")).toBool(true) : true},
        {QStringLiteral("freeFlyCameraEnabled"), root.contains(QStringLiteral("freeFlyCameraEnabled")) ? root.value(QStringLiteral("freeFlyCameraEnabled")).toBool(true) : true},
        {QStringLiteral("viewportCount"), root.contains(QStringLiteral("viewportCount")) ? root.value(QStringLiteral("viewportCount")).toInt(1) : 1},
        {QStringLiteral("viewportCameraIds"), root.value(QStringLiteral("viewportCameraIds")).toArray()}
    };
}

void ProjectSession::applyStateObject(const QString& projectId, const QJsonObject& state)
{
    m_currentProjectId = state.value(QStringLiteral("projectId")).toString(projectId);

    const QString savedRoot = state.value(QStringLiteral("projectRoot")).toString();
    const QFileInfo rootInfo(savedRoot);
    if (rootInfo.exists() && rootInfo.isDir())
    {
        m_currentProjectRoot = rootInfo.absoluteFilePath();
    }
    else
    {
        m_currentProjectRoot = QDir::currentPath();
    }

    m_currentGalleryPath = state.value(QStringLiteral("galleryPath")).toString();
    m_currentSelectedAssetPath = state.value(QStringLiteral("selectedAssetPath")).toString();
    m_currentViewportAssetPath = state.value(QStringLiteral("viewportAssetPath")).toString();
    m_currentSceneItems = state.value(QStringLiteral("sceneItems")).toArray();
    m_currentCameraConfigs = state.value(QStringLiteral("cameraConfigs")).toArray();

    // Load camera position
    const QJsonArray cameraPosArray = state.value(QStringLiteral("cameraPosition")).toArray();
    if (cameraPosArray.size() == 3) {
        m_currentCameraPosition = QVector3D(
            cameraPosArray[0].toDouble(0.0f),
            cameraPosArray[1].toDouble(0.0f),
            cameraPosArray[2].toDouble(3.0f)
        );
    } else {
        m_currentCameraPosition = QVector3D(0.0f, 0.0f, 3.0f); // default
    }

    // Load camera rotation
    const QJsonArray cameraRotArray = state.value(QStringLiteral("cameraRotation")).toArray();
    if (cameraRotArray.size() == 3) {
        m_currentCameraRotation = QVector3D(
            cameraRotArray[0].toDouble(0.0f),
            cameraRotArray[1].toDouble(0.0f),
            cameraRotArray[2].toDouble(0.0f)
        );
    } else {
        m_currentCameraRotation = QVector3D(0.0f, 0.0f, 0.0f); // default
    }
    m_currentCameraSpeed = static_cast<float>(state.value(QStringLiteral("cameraSpeed")).toDouble(0.01));
    m_currentRenderPath = state.value(QStringLiteral("renderPath")).toString(QStringLiteral("forward3d"));
    m_currentMeshConsolidationEnabled = state.value(QStringLiteral("meshConsolidationEnabled")).toBool(true);
    m_currentValidationLayersEnabled = state.value(QStringLiteral("validationLayersEnabled")).toBool(true);
    m_currentFreeFlyCameraEnabled = state.value(QStringLiteral("freeFlyCameraEnabled")).toBool(true);
    m_currentViewportCount = std::clamp(state.value(QStringLiteral("viewportCount")).toInt(1), 1, 4);
    m_currentViewportCameraIds = state.value(QStringLiteral("viewportCameraIds")).toArray();
    m_currentSceneLight = state.value(QStringLiteral("sceneLight")).toObject();
    if (m_currentSceneItems.isEmpty())
    {
        const QJsonArray scenePaths = state.value(QStringLiteral("sceneAssetPaths")).toArray();
        for (const QJsonValue& value : scenePaths)
        {
            if (value.isString())
            {
                const QString sourcePath = value.toString();
                m_currentSceneItems.push_back(QJsonObject{
                    {QStringLiteral("name"), QFileInfo(sourcePath).completeBaseName()},
                    {QStringLiteral("sourcePath"), sourcePath},
                    {QStringLiteral("translation"), QJsonArray{0.0, 0.0, 0.0}},
                    {QStringLiteral("rotation"), QJsonArray{-90.0, 0.0, 0.0}},
                    {QStringLiteral("scale"), QJsonArray{1.0, 1.0, 1.0}}
                });
            }
        }
    }
}

}  // namespace motive::ui
