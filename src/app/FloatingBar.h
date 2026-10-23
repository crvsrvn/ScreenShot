#pragma once

#include <QPoint>
#include <QWidget>

class QMimeData;

namespace ss {

// 桌面悬浮工具栏（GEN-08）：常驻置顶的小条，一键截图 / 贴图；把图片或文字拖到上面直接贴图。可拖动，位置记忆。
class FloatingBar : public QWidget {
    Q_OBJECT
public:
    explicit FloatingBar(QWidget* parent = nullptr);

signals:
    void captureRequested();
    void pinRequested();
    void dropped(const QMimeData* mime, QPoint physicalPos); // 拖入内容：在条的位置附近贴图
    void moved(QPoint logicalPos);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    QPoint m_dragOffset;
    bool m_dragging = false;
};

} // namespace ss
