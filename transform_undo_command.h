#pragma once

#include <QUndoCommand>
#include <QVector3D>
#include <QString>
#include <functional>

namespace motive::ui {

// Undo command for scene item transform operations
class TransformUndoCommand : public QUndoCommand
{
public:
    // Callback signature: void(index, translation, rotation, scale)
    using ApplyTransformCallback = std::function<void(int, const QVector3D&, const QVector3D&, const QVector3D&)>;

    TransformUndoCommand(
        int sceneIndex,
        const QVector3D& oldTranslation,
        const QVector3D& oldRotation,
        const QVector3D& oldScale,
        const QVector3D& newTranslation,
        const QVector3D& newRotation,
        const QVector3D& newScale,
        const QString& itemName,
        ApplyTransformCallback applyCallback,
        QUndoCommand* parent = nullptr
    );

    void undo() override;
    void redo() override;

    int id() const override { return 1; }  // Command ID for potential merging
    bool mergeWith(const QUndoCommand* other) override;

private:
    int m_sceneIndex;
    QVector3D m_oldTranslation;
    QVector3D m_oldRotation;
    QVector3D m_oldScale;
    QVector3D m_newTranslation;
    QVector3D m_newRotation;
    QVector3D m_newScale;
    ApplyTransformCallback m_applyCallback;
    bool m_firstRedo = true;  // Skip first redo since the action already happened
};

}  // namespace motive::ui
