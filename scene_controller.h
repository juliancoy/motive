#pragma once

#include "host_widget.h"

#include <QList>
#include <QString>

namespace motive::ui {

class ViewportRuntime;

class ViewportSceneController
{
public:
    explicit ViewportSceneController(ViewportRuntime& runtime);

    void loadAssetFromPath(const QString& path);
    void loadSceneFromItems(const QList<ViewportHostWidget::SceneItem>& items);
    void addAssetToScene(const QString& path);

    QString currentAssetPath() const;
    QList<ViewportHostWidget::SceneItem> sceneItems() const;

    bool meshConsolidationEnabled() const;
    void setMeshConsolidationEnabled(bool enabled);

    void updateSceneItemTransform(int index, const QVector3D& translation, const QVector3D& rotation, const QVector3D& scale);
    void setSceneItemMeshConsolidationEnabled(int index, bool enabled);
    void updateSceneItemPaintOverride(int index, bool enabled, const QVector3D& color);
    void updateSceneItemAnimationState(int index, const QString& activeClip, bool playing, bool loop, float speed);
    void updateSceneItemAnimationPhysicsCoupling(int index, const QString& couplingMode);
    void updateSceneItemPhysicsGravity(int index, bool useGravity, const QVector3D& customGravity);
    void updateSceneItemCharacterTurnResponsiveness(int index, float responsiveness);
    void renameSceneItem(int index, const QString& name);
    void setSceneItemVisible(int index, bool visible);
    void deleteSceneItem(int index);

    QList<ViewportHostWidget::SceneItem>& loadedEntries();
    const QList<ViewportHostWidget::SceneItem>& loadedEntries() const;
    QList<ViewportHostWidget::SceneItem>& pendingEntries();
    const QList<ViewportHostWidget::SceneItem>& pendingEntries() const;

    void restorePendingEntries();

private:
    ViewportRuntime& m_runtime;
    QString m_currentAssetPath;
    QList<ViewportHostWidget::SceneItem> m_sceneEntries;
    QList<ViewportHostWidget::SceneItem> m_pendingSceneEntries;
    bool m_meshConsolidationEnabled = true;
};

}  // namespace motive::ui
