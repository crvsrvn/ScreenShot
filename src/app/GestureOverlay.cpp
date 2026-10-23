#include "app/GestureOverlay.h"

#include <QGuiApplication>
#include <QPainter>
#include <QScreen>

namespace ss {

GestureOverlay::GestureOverlay()
    : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
}

void GestureOverlay::showRegion(const QRect& physical)
{
    m_physical = physical;
    QRect bounds;
    for (QScreen* s : QGuiApplication::screens())
        bounds = bounds.united(s->geometry());
    if (geometry() != bounds)
        setGeometry(bounds);
    if (!isVisible())
        show();
    update();
}

void GestureOverlay::hideRegion()
{
    m_physical = QRect();
    hide();
}

void GestureOverlay::paintEvent(QPaintEvent*)
{
    if (m_physical.isEmpty())
        return;
    QPainter p(this);
    // 物理 → 逻辑：按所在屏幕的缩放
    QScreen* s = QGuiApplication::screenAt(m_physical.topLeft());
    const qreal dpr = s ? s->devicePixelRatio() : 1.0;
    const QPoint origin = s ? s->geometry().topLeft() : QPoint();
    const QRectF logical(QPointF(origin) + QPointF(m_physical.topLeft() - origin) / dpr - QPointF(pos()), QSizeF(m_physical.size()) / dpr);
    QPen pen(QColor(40, 120, 220), 2);
    pen.setCosmetic(true);
    p.setPen(pen);
    p.setBrush(QColor(40, 120, 220, 40));
    p.drawRect(logical);
}

} // namespace ss
