#include "shell.h"
#include "host_widget.h"

#include <QDir>
#include <QFont>
#include <QTreeWidget>
#include <QTreeWidgetItem>

namespace motive::ui {

void MainWindowShell::refreshHierarchy(const QList<ViewportHostWidget::SceneItem>& items)
{
    if (!m_hierarchyTree)
    {
        return;
    }

    m_sceneItems = items;
    QTreeWidgetItem* previousItem = m_hierarchyTree->currentItem();
    const int previousRow = previousItem ? previousItem->data(0, Qt::UserRole).toInt() : -1;
    const int previousMeshIndex = previousItem ? previousItem->data(0, Qt::UserRole + 1).toInt() : -1;
    const int previousPrimitiveIndex = previousItem ? previousItem->data(0, Qt::UserRole + 2).toInt() : -1;
    const int previousType = previousItem ? previousItem->data(0, Qt::UserRole + 3).toInt() : -1;
    const QString previousClipName = previousItem ? previousItem->data(0, Qt::UserRole + 4).toString() : QString();
    const int previousCameraIndex = previousItem ? previousItem->data(0, Qt::UserRole + 5).toInt() : -1;
    const QString previousCameraId = previousItem ? previousItem->data(0, Qt::UserRole + 6).toString() : QString();
    m_hierarchyTree->clear();

    const QList<ViewportHostWidget::HierarchyNode> hierarchyItems =
        m_viewportHost ? m_viewportHost->hierarchyItems() : QList<ViewportHostWidget::HierarchyNode>{};
    for (const auto& node : hierarchyItems)
    {
        appendHierarchyNode(nullptr, node, false);
    }

    if (previousItem)
    {
        QList<QTreeWidgetItem*> matches = m_hierarchyTree->findItems(QStringLiteral("*"), Qt::MatchWildcard | Qt::MatchRecursive);
        QTreeWidgetItem* fallbackRowMatch = nullptr;
        for (QTreeWidgetItem* item : matches)
        {
            if (!item || item->data(0, Qt::UserRole).toInt() != previousRow)
            {
                continue;
            }
            if (!fallbackRowMatch)
            {
                fallbackRowMatch = item;
            }
            if (item->data(0, Qt::UserRole + 1).toInt() == previousMeshIndex &&
                item->data(0, Qt::UserRole + 2).toInt() == previousPrimitiveIndex &&
                item->data(0, Qt::UserRole + 3).toInt() == previousType &&
                item->data(0, Qt::UserRole + 4).toString() == previousClipName &&
                item->data(0, Qt::UserRole + 5).toInt() == previousCameraIndex &&
                item->data(0, Qt::UserRole + 6).toString() == previousCameraId)
            {
                m_hierarchyTree->setCurrentItem(item);
                return;
            }
        }
        if (fallbackRowMatch)
        {
            m_hierarchyTree->setCurrentItem(fallbackRowMatch);
            return;
        }
    }

    if (!items.isEmpty())
    {
        m_hierarchyTree->setCurrentItem(m_hierarchyTree->topLevelItem(0));
    }
    else
    {
        updateInspectorForSelection(nullptr);
    }
}

void MainWindowShell::appendHierarchyNode(QTreeWidgetItem* parent, const ViewportHostWidget::HierarchyNode& node, bool ancestorHidden)
{
    if (!m_hierarchyTree)
    {
        return;
    }

    QTreeWidgetItem* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_hierarchyTree);
    item->setText(0, node.label);
    item->setData(0, Qt::UserRole, node.sceneIndex);
    item->setData(0, Qt::UserRole + 1, node.meshIndex);
    item->setData(0, Qt::UserRole + 2, node.primitiveIndex);
    item->setData(0, Qt::UserRole + 3, static_cast<int>(node.type));
    item->setData(0, Qt::UserRole + 4, node.clipName);
    item->setData(0, Qt::UserRole + 5, node.cameraIndex);  // Camera index for camera nodes
    item->setData(0, Qt::UserRole + 6, node.cameraId);
    const bool selfHidden = node.sceneIndex >= 0 && node.sceneIndex < m_sceneItems.size() && !m_sceneItems[node.sceneIndex].visible;
    const bool hidden = ancestorHidden || selfHidden;
    QFont font = item->font(0);
    font.setItalic(hidden);
    item->setFont(0, font);

    if (node.children.isEmpty() && node.sceneIndex >= 0 && node.sceneIndex < m_sceneItems.size())
    {
        const auto& sceneItem = m_sceneItems[node.sceneIndex];
        item->setToolTip(0, QDir::toNativeSeparators(sceneItem.sourcePath));
    }

    for (const auto& child : node.children)
    {
        appendHierarchyNode(item, child, hidden);
    }

    if (!node.children.isEmpty())
    {
        item->setExpanded(true);
    }
}

}  // namespace motive::ui
