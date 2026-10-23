#pragma once

#include "core/util/Mipmap.h"

#include <QImage>
#include <QPixmap>
#include <QTimer>
#include <QWidget>

namespace ss {

// 贴图窗口（M0 原型，验证 R-05 / R-07）：无边框、置顶、透明背景、带缓存阴影。
// - 窗口只覆盖内容在桌面上可见的部分（超出屏幕的大图不会产生超大的分层窗口）
// - 拖动时若可见部分不变只移动窗口，不重绘
// - 缩放交互中用多级缩小图 + 快速变换，停下后再做一次平滑缩放
// - 阴影按九宫格缓存，与内容尺寸无关
class PinWindow : public QWidget {
    Q_OBJECT
public:
    explicit PinWindow(const QImage& image);

    // 内容左上角（逻辑桌面坐标）
    void moveContentTo(QPointF logicalTopLeft);
    QPointF contentTopLeft() const { return m_contentTopLeft; }
    void moveToPhysical(QPoint physicalTopLeft);

    qreal scale() const { return m_scale; }
    // 以窗口内 anchor 为中心缩放；interactive=true 表示交互中（延迟平滑）
    void zoomAt(QPointF anchorLogical, qreal factor, bool interactive);
    void setScale(qreal scale, bool interactive);
    // 内容超出所在屏幕时缩小到能放下（PIN-42）
    void fitToScreen();

    const QImage& image() const { return m_mipmap.original(); }
    QRectF contentRect() const; // 逻辑桌面坐标
    bool isClipped() const { return m_clipped; }
    int paintCount() const { return m_paintCount; }
    qint64 lastPaintNs() const { return m_lastPaintNs; }
    QList<double> takePaintDurationsMs();

signals:
    void closed();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    QSizeF contentSize() const;
    QRectF desktopBounds() const;
    void relayout();
    void drawShadow(QPainter& painter, const QRectF& content);
    void finishInteraction();

    static constexpr int kShadowMargin = 24;

    Mipmap m_mipmap;
    QPointF m_contentTopLeft;
    qreal m_scale = 1.0;
    bool m_interactive = false;
    bool m_clipped = false;
    QPixmap m_smooth;     // 停止交互后的平滑缩放缓存（只在尺寸有限时使用）
    QTimer m_settleTimer;
    QPointF m_dragOffset;
    bool m_dragging = false;
    int m_paintCount = 0;
    qint64 m_lastPaintNs = 0;
    QList<double> m_paintDurationsMs;
};

} // namespace ss
