#pragma once

#include "host_widget.h"

#include <QJsonArray>

namespace motive::ui {

class ViewportRuntime;
class ViewportSceneController;

class ViewportHierarchyBuilder
{
public:
    ViewportHierarchyBuilder(ViewportRuntime& runtime, const ViewportSceneController& sceneController, const ViewportHostWidget::SceneLight& sceneLight);

    QList<ViewportHostWidget::HierarchyNode> hierarchyItems() const;
    QJsonArray hierarchyJson() const;
    QJsonArray sceneProfileJson() const;

private:
    ViewportRuntime& m_runtime;
    const ViewportSceneController& m_sceneController;
    const ViewportHostWidget::SceneLight& m_sceneLight;
};

}  // namespace motive::ui
