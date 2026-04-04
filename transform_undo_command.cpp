#include "transform_undo_command.h"

namespace motive::ui {

TransformUndoCommand::TransformUndoCommand(
    int sceneIndex,
    const QVector3D& oldTranslation,
    const QVector3D& oldRotation,
    const QVector3D& oldScale,
    const QVector3D& newTranslation,
    const QVector3D& newRotation,
    const QVector3D& newScale,
    const QString& itemName,
    ApplyTransformCallback applyCallback,
    QUndoCommand* parent)
    : QUndoCommand(
        QStringLiteral("Move %1").arg(itemName.isEmpty() ? QStringLiteral("Item") : itemName),
        parent)
    , m_sceneIndex(sceneIndex)
    , m_oldTranslation(oldTranslation)
    , m_oldRotation(oldRotation)
    , m_oldScale(oldScale)
    , m_newTranslation(newTranslation)
    , m_newRotation(newRotation)
    , m_newScale(newScale)
    , m_applyCallback(std::move(applyCallback))
{
}

void TransformUndoCommand::undo()
{
    if (m_applyCallback)
    {
        m_applyCallback(m_sceneIndex, m_oldTranslation, m_oldRotation, m_oldScale);
    }
}

void TransformUndoCommand::redo()
{
    // Skip the first redo because the action has already been performed
    // when the command was created
    if (m_firstRedo)
    {
        m_firstRedo = false;
        return;
    }
    
    if (m_applyCallback)
    {
        m_applyCallback(m_sceneIndex, m_newTranslation, m_newRotation, m_newScale);
    }
}

bool TransformUndoCommand::mergeWith(const QUndoCommand* other)
{
    // Merge with another transform command on the same item
    const auto* otherCmd = dynamic_cast<const TransformUndoCommand*>(other);
    if (!otherCmd)
        return false;
    
    if (otherCmd->m_sceneIndex != m_sceneIndex)
        return false;
    
    // Update our "new" state to be the other command's new state
    m_newTranslation = otherCmd->m_newTranslation;
    m_newRotation = otherCmd->m_newRotation;
    m_newScale = otherCmd->m_newScale;
    
    return true;
}

}  // namespace motive::ui
