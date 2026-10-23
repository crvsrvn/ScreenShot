#pragma once

#include "core/ports/ScreenTypes.h"

#include <QWidget>

namespace ss {

class CaptureSession;

// 覆盖窗口：每台显示器一个，启动时创建并隐藏，截图时换背景并显示（NFR-01）。
// 只做两件事：把 Qt 的输入换算成物理像素交给会话；按会话状态绘制（背景、遮罩、高亮、选区、锚点、放大镜、历史图）。
class OverlayWindow : public QWidget {
    Q_OBJECT
public:
    explicit OverlayWindow(CaptureSession* session);

    void setScreenInfo(const ScreenInfo& info);
    const ScreenInfo& screenInfo() const { return m_screen; }

    void setSnapshot(const ScreenSnapshot& snapshot);
    void clearSnapshot();
    void showOnScreen();

    // 坐标换算
    QPoint toPhysical(QPointF logicalInWindow) const;
    QRectF toLogical(const QRect& physical) const;       // 窗口内逻辑坐标
    QRectF toLogicalGlobal(const QRect& physical) const; // 全局逻辑坐标（用于放置其他窗口）

    QImage renderToImage(); // 组件测试用，物理像素尺寸
    int paintCount() const { return m_paintCount; }

signals:
    void firstPainted(qint64 atNs);

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    bool event(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

private:
    void paintContent(QPainter& painter);
    void paintSelection(QPainter& painter, const QRect& selection);
    void paintMagnifierIfNeeded(QPainter& painter);
    void updateCursorShape(QPoint physical);
    void moveCursorTo(QPoint physical);

    CaptureSession* m_session;
    ScreenInfo m_screen;
    QImage m_image; // devicePixelRatio == m_screen.scale
    bool m_awaitingFirstPaint = false;
    int m_paintCount = 0;
};

} // namespace ss
