#include "core/annotation/Overlays.h"

#include <QDateTime>
#include <QFontMetricsF>
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

// ---- 水印 ----

QString WatermarkAnnotation::expandedText() const
{
    const QDateTime now = QDateTime::currentDateTime();
    QString t = m_text;
    t.replace(QStringLiteral("{date}"), now.toString(QStringLiteral("yyyy-MM-dd")));
    t.replace(QStringLiteral("{time}"), now.toString(QStringLiteral("HH:mm:ss")));
    t.replace(QStringLiteral("{datetime}"), now.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    return t;
}

void WatermarkAnnotation::paint(QPainter& p, const RenderContext& ctx) const
{
    const QSize canvas = ctx.below ? ctx.below->size() : m_canvas;
    if (canvas.isEmpty())
        return;
    const QString text = expandedText();
    if (text.isEmpty())
        return;
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.setFont(m_font);
    QColor c = m_style.stroke;
    c.setAlpha(m_alpha);
    p.setPen(c);
    const QFontMetricsF fm(m_font);
    const QSizeF textSize(fm.horizontalAdvance(text), fm.height());
    const int margin = 12;
    if (m_placement == Placement::Tiled) {
        // 旋转后平铺整幅画布
        p.translate(canvas.width() / 2.0, canvas.height() / 2.0);
        p.rotate(m_angle);
        const double diag = std::hypot(canvas.width(), canvas.height());
        const double stepX = textSize.width() + m_spacing;
        const double stepY = textSize.height() + m_spacing;
        int row = 0;
        for (double y = -diag; y < diag; y += stepY, ++row) {
            const double offset = (row % 2) ? stepX / 2 : 0;
            for (double x = -diag + offset; x < diag; x += stepX)
                p.drawText(QPointF(x, y), text);
        }
        p.restore();
        return;
    }
    QRectF box(0, 0, textSize.width(), textSize.height());
    const double w = canvas.width(), h = canvas.height();
    switch (m_placement) {
    case Placement::TopLeft: box.moveTopLeft(QPointF(margin, margin)); break;
    case Placement::Top: box.moveTopLeft(QPointF((w - box.width()) / 2, margin)); break;
    case Placement::TopRight: box.moveTopLeft(QPointF(w - box.width() - margin, margin)); break;
    case Placement::Left: box.moveTopLeft(QPointF(margin, (h - box.height()) / 2)); break;
    case Placement::Center: box.moveTopLeft(QPointF((w - box.width()) / 2, (h - box.height()) / 2)); break;
    case Placement::Right: box.moveTopLeft(QPointF(w - box.width() - margin, (h - box.height()) / 2)); break;
    case Placement::BottomLeft: box.moveTopLeft(QPointF(margin, h - box.height() - margin)); break;
    case Placement::Bottom: box.moveTopLeft(QPointF((w - box.width()) / 2, h - box.height() - margin)); break;
    case Placement::BottomRight:
    case Placement::Tiled: box.moveTopLeft(QPointF(w - box.width() - margin, h - box.height() - margin)); break;
    }
    p.drawText(box, Qt::AlignLeft | Qt::AlignVCenter, text);
    p.restore();
}

QString WatermarkAnnotation::placementName(Placement pl)
{
    static const char* names[] = {"tiled", "topLeft", "top", "topRight", "left", "center", "right", "bottomLeft", "bottom", "bottomRight"};
    return QLatin1String(names[static_cast<int>(pl)]);
}

WatermarkAnnotation::Placement WatermarkAnnotation::placementFromName(const QString& name)
{
    for (int i = 0; i <= static_cast<int>(Placement::BottomRight); ++i)
        if (placementName(static_cast<Placement>(i)) == name)
            return static_cast<Placement>(i);
    return Placement::BottomRight;
}

QJsonObject WatermarkAnnotation::toJson() const
{
    QJsonObject o = Annotation::toJson();
    o[QStringLiteral("text")] = m_text;
    o[QStringLiteral("placement")] = placementName(m_placement);
    o[QStringLiteral("alpha")] = m_alpha;
    o[QStringLiteral("font")] = m_font.toString();
    o[QStringLiteral("spacing")] = m_spacing;
    o[QStringLiteral("angle")] = m_angle;
    o[QStringLiteral("canvas")] = QJsonArray{m_canvas.width(), m_canvas.height()};
    return o;
}

void WatermarkAnnotation::readJson(const QJsonObject& o)
{
    Annotation::readJson(o);
    m_text = o.value(QStringLiteral("text")).toString();
    m_placement = placementFromName(o.value(QStringLiteral("placement")).toString());
    m_alpha = o.value(QStringLiteral("alpha")).toInt(110);
    m_font.fromString(o.value(QStringLiteral("font")).toString());
    m_spacing = o.value(QStringLiteral("spacing")).toInt(120);
    m_angle = o.value(QStringLiteral("angle")).toDouble(-30);
    const QJsonArray c = o.value(QStringLiteral("canvas")).toArray();
    if (c.size() == 2)
        m_canvas = QSize(c[0].toInt(), c[1].toInt());
}

} // namespace ss
