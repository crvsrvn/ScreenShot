#pragma once

#include "core/ports/ScreenTypes.h"

#include <QWidget>

namespace ss {

class CaptureSession;

// 覆盖窗口：每台显示器一个，启动时创建并隐藏，截图时换背景并显示（NFR-01）。
// 内部全部用物理像素与会话交互；只有与 Qt 的输入和绘制接口打交道时才换算逻辑坐标。
class OverlayWindow : public QWidget {
    Q_OBJECT
public:
    explicit OverlayWindow(CaptureSession* session);

    void setScreenInfo(const ScreenInfo& info);
    const ScreenInfo& screenInfo() const { return m_screen; }

    void setSnapshot(const ScreenSnapshot& snapshot);
    void clearSnapshot();

    // 定位到所属显示器并显示
    void showOnScreen();

    // 逻辑坐标（窗口内）→ 物理坐标（虚拟桌面）
    QPoint toPhysical(QPointF logical) const;
    // 物理矩形 → 逻辑矩形（窗口内）
    QRectF toLogical(const QRect& physical) const;

    // 把当前内容画成图像（组件测试用），物理像素尺寸
    QImage renderToImage();

    int paintCount() const { return m_paintCount; }

signals:
    void firstPainted(qint64 atNs);

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void paintContent(QPainter& painter);

    CaptureSession* m_session;
    ScreenInfo m_screen;
    QImage m_image; // devicePixelRatio == m_screen.scale
    bool m_awaitingFirstPaint = false;
    int m_paintCount = 0;
};

} // namespace ss
