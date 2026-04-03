#include "engine_ui_project_session.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

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

QVector3D ProjectSession::currentCameraPosition() const
{
    return m_currentCameraPosition;
}

QVector3D ProjectSession::currentCameraRotation() const
{
    return m_currentCameraRotation;
}

QString ProjectSession::currentRenderPath() const
{
    return m_currentRenderPath;
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

void ProjectSession::setCurrentCameraPosition(const QVector3D& position)
{
    m_currentCameraPosition = position;
}

void ProjectSession::setCurrentCameraRotation(const QVector3D& rotation)
{
    m_currentCameraRotation = rotation;
}

void ProjectSession::setCurrentRenderPath(const QString& renderPath)
{
    m_currentRenderPath = renderPath.trimmed().isEmpty() ? QStringLiteral("forward3d") : renderPath;
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
    saveCurrentProjectMarker();
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
        {QStringLiteral("cameraPosition"), QJsonArray{m_currentCameraPosition.x(), m_currentCameraPosition.y(), m_currentCameraPosition.z()}},
        {QStringLiteral("cameraRotation"), QJsonArray{m_currentCameraRotation.x(), m_currentCameraRotation.y(), m_currentCameraRotation.z()}},
        {QStringLiteral("renderPath"), m_currentRenderPath}
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
    appendSetIfChanged(QStringLiteral("cameraPosition"), QJsonArray{m_currentCameraPosition.x(), m_currentCameraPosition.y(), m_currentCameraPosition.z()});
    appendSetIfChanged(QStringLiteral("cameraRotation"), QJsonArray{m_currentCameraRotation.x(), m_currentCameraRotation.y(), m_currentCameraRotation.z()});
    appendSetIfChanged(QStringLiteral("renderPath"), m_currentRenderPath);

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
        {QStringLiteral("cameraPosition"), QJsonArray{0.0, 0.0, 3.0}},
        {QStringLiteral("cameraRotation"), QJsonArray{0.0, 0.0, 0.0}},
        {QStringLiteral("renderPath"), root.value(QStringLiteral("renderPath")).toString(QStringLiteral("forward3d"))}
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
    m_currentRenderPath = state.value(QStringLiteral("renderPath")).toString(QStringLiteral("forward3d"));
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
