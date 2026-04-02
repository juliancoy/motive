#pragma once

#include <QTimer>
#include <QWidget>

class QLabel;

namespace motive::ui {

class ViewportHostWidget : public QWidget
{
public:
    explicit ViewportHostWidget(QWidget* parent = nullptr);
    ~ViewportHostWidget() override;

    void loadAssetFromPath(const QString& path);

protected:
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    void ensureViewportInitialized();
    void renderFrame();
    void embedNativeWindow();
    void syncEmbeddedWindowGeometry();

    QTimer m_renderTimer;
    bool m_initialized = false;
    bool m_initScheduled = false;
    QLabel* m_statusLabel = nullptr;
};

}  // namespace motive::ui
