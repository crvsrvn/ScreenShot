#pragma once

#include "core/util/Mipmap.h"

#include <QImage>
#include <QPixmap>
#include <QTimer>
#include <QWidget>

namespace ss {

// 贴图窗口（M0 原型，验证 R-05 / R-07）：无边框、置顶、透明背景、带缓存阴影。
// 拖动只移动窗口不重绘；缩放交互中用多级缩小图 + 快速变换，停下后再做一次平滑缩放。
class PinWindow : public QWidget {
    Q_OBJECT
public:
    explicit PinWindow(const QImage& image);

    void moveToPhysical(QPoint physicalTopLeft);
    qreal scale() const { return m_scale; }
    // 以窗口内 anchor 为中心缩放；interactive=true 表示交互中（延迟平滑）
    void zoomAt(QPointF anchorLogical, qreal factor, bool interactive);
    void setScale(qreal scale, bool interactive);

    const QImage& image() const { return m_mipmap.original(); }
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
    void relayout(QPointF keepAnchorLogical, QPointF keepAnchorRatio);
    void ensureShadow();
    void finishInteraction();

    static constexpr int kShadowMargin = 24;

    Mipmap m_mipmap;
    qreal m_scale = 1.0;
    bool m_interactive = false;
    QPixmap m_shadow;     // 按内容尺寸缓存
    QSize m_shadowFor;
    QPixmap m_smooth;     // 停止交互后的平滑缩放缓存
    QTimer m_settleTimer;
    QPoint m_dragOffset;
    bool m_dragging = false;
    int m_paintCount = 0;
    qint64 m_lastPaintNs = 0;
    QList<double> m_paintDurationsMs;
};

} // namespace ss
