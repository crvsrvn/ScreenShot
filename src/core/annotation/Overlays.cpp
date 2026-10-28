#include "core/annotation/Overlays.h"

#include <QJsonArray>

namespace ss {

// ---- 聚光灯 ----

QPainterPath SpotlightAnnotation::shapePath() const
{
    QPainterPath path;
    if (m_shape == Shape::Ellipse)
        path.addEllipse(m_rect);
    else
        path.addRect(m_rect);
    return path;
}

bool SpotlightAnnotation::hitTest(QPointF p, double tolerance) const
{
    return m_rect.adjusted(-tolerance, -tolerance, tolerance, tolerance).contains(p);
}

void SpotlightAnnotation::paint(QPainter& p, const RenderContext& ctx) const
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF canvas = ctx.below ? QRectF(ctx.below->rect()) : m_rect.adjusted(-10000, -10000, 10000, 10000);
    QPainterPath outside;
    outside.addRect(canvas);
    outside = outside.subtracted(shapePath());
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, m_maskAlpha));
    p.drawPath(outside);
    if (m_style.width > 0) {
        p.setPen(pen());
        p.setBrush(Qt::NoBrush);
        p.drawPath(shapePath());
    }
    p.restore();
}

QJsonObject SpotlightAnnotation::toJson() const
{
    QJsonObject o = Annotation::toJson();
    o[QStringLiteral("rect")] = QJsonArray{m_rect.x(), m_rect.y(), m_rect.width(), m_rect.height()};
    o[QStringLiteral("shape")] = static_cast<int>(m_shape);
    o[QStringLiteral("maskAlpha")] = m_maskAlpha;
    return o;
}

void SpotlightAnnotation::readJson(const QJsonObject& o)
{
    Annotation::readJson(o);
    const QJsonArray r = o.value(QStringLiteral("rect")).toArray();
    if (r.size() == 4)
        m_rect = QRectF(r[0].toDouble(), r[1].toDouble(), r[2].toDouble(), r[3].toDouble());
    m_shape = static_cast<Shape>(o.value(QStringLiteral("shape")).toInt());
    m_maskAlpha = o.value(QStringLiteral("maskAlpha")).toInt(150);
}

} // namespace ss
