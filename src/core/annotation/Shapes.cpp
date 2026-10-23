#include "core/annotation/Shapes.h"

#include "core/fx/Blur.h"
#include "core/fx/Mosaic.h"

#include <QFontMetricsF>
#include <QJsonArray>
#include <QPainterPathStroker>
#include <cmath>

namespace ss {

// ---- 工具 ----

QPainterPath brushPath(const QList<QPointF>& points, double width, bool smooth)
{
    QPainterPath path;
    if (points.isEmpty())
        return path;
    if (points.size() == 1) {
        path.addEllipse(points.first(), width / 2, width / 2);
        return path;
    }
    QPainterPath line(points.first());
    if (smooth && points.size() > 2) {
        for (int i = 1; i < points.size() - 1; ++i) {
            const QPointF mid = (points[i] + points[i + 1]) / 2;
            line.quadTo(points[i], mid);
        }
        line.lineTo(points.last());
    } else {
        for (int i = 1; i < points.size(); ++i)
            line.lineTo(points[i]);
    }
    QPainterPathStroker stroker;
    stroker.setWidth(width);
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);
    return stroker.createStroke(line);
}

QJsonArray pointsToJson(const QList<QPointF>& points)
{
    QJsonArray a;
    for (const auto& p : points) {
        a.append(p.x());
        a.append(p.y());
    }
    return a;
}

QList<QPointF> pointsFromJson(const QJsonArray& array)
{
    QList<QPointF> points;
    for (int i = 0; i + 1 < array.size(); i += 2)
        points.append(QPointF(array[i].toDouble(), array[i + 1].toDouble()));
    return points;
}

QRectF boundsOfPoints(const QList<QPointF>& points, double pad)
{
    if (points.isEmpty())
        return {};
    double l = points.first().x(), t = points.first().y(), r = l, b = t;
    for (const auto& p : points) {
        l = std::min(l, p.x());
        t = std::min(t, p.y());
        r = std::max(r, p.x());
        b = std::max(b, p.y());
    }
    return QRectF(QPointF(l, t), QPointF(r, b)).adjusted(-pad, -pad, pad, pad);
}

void scalePoints(QList<QPointF>& points, const QRectF& from, const QRectF& to)
{
    if (from.width() <= 0 || from.height() <= 0)
        return;
    const double sx = to.width() / from.width();
    const double sy = to.height() / from.height();
    for (auto& p : points)
        p = QPointF(to.left() + (p.x() - from.left()) * sx, to.top() + (p.y() - from.top()) * sy);
}

// ---- 矩形 / 椭圆 ----

bool RectAnnotation::hitTest(QPointF p, double tolerance) const
{
    const QPointF local = rotationTransform().inverted().map(p);
    const double t = tolerance + m_style.width / 2.0;
    if (m_style.filled)
        return m_rect.adjusted(-t, -t, t, t).contains(local);
    return m_rect.adjusted(-t, -t, t, t).contains(local) && !m_rect.adjusted(t, t, -t, -t).contains(local);
}

void RectAnnotation::paint(QPainter& p, const RenderContext&) const
{
    p.save();
    applyRotation(p);
    p.setRenderHint(QPainter::Antialiasing, true);
    if (m_style.filled) {
        p.setPen(Qt::NoPen);
        p.setBrush(m_style.fill);
    } else {
        p.setPen(pen());
        p.setBrush(Qt::NoBrush);
    }
    if (radius() > 0)
        p.drawRoundedRect(m_rect, radius(), radius());
    else
        p.drawRect(m_rect);
    p.restore();
}

QJsonObject RectAnnotation::toJson() const
{
    QJsonObject o = Annotation::toJson();
    o[QStringLiteral("rect")] = QJsonArray{m_rect.x(), m_rect.y(), m_rect.width(), m_rect.height()};
    o[QStringLiteral("radius")] = m_radius;
    return o;
}

void RectAnnotation::readJson(const QJsonObject& o)
{
    Annotation::readJson(o);
    const QJsonArray r = o.value(QStringLiteral("rect")).toArray();
    if (r.size() == 4)
        m_rect = QRectF(r[0].toDouble(), r[1].toDouble(), r[2].toDouble(), r[3].toDouble());
    m_radius = o.value(QStringLiteral("radius")).toDouble();
}

bool EllipseAnnotation::hitTest(QPointF p, double tolerance) const
{
    const QPointF local = rotationTransform().inverted().map(p);
    const double t = tolerance + m_style.width / 2.0;
    QPainterPath outer;
    outer.addEllipse(m_rect.adjusted(-t, -t, t, t));
    if (m_style.filled)
        return outer.contains(local);
    QPainterPath inner;
    inner.addEllipse(m_rect.adjusted(t, t, -t, -t));
    return outer.contains(local) && !inner.contains(local);
}

void EllipseAnnotation::paint(QPainter& p, const RenderContext&) const
{
    p.save();
    applyRotation(p);
    p.setRenderHint(QPainter::Antialiasing, true);
    if (m_style.filled) {
        p.setPen(Qt::NoPen);
        p.setBrush(m_style.fill);
    } else {
        p.setPen(pen());
        p.setBrush(Qt::NoBrush);
    }
    p.drawEllipse(m_rect);
    p.restore();
}

// ---- 线条 / 箭头 ----

QRectF LineAnnotation::bounds() const
{
    return boundsOfPoints(m_points, 0);
}

void LineAnnotation::setBounds(const QRectF& r)
{
    scalePoints(m_points, bounds(), r);
}

void LineAnnotation::translate(QPointF d)
{
    for (auto& p : m_points)
        p += d;
}

bool LineAnnotation::hitTest(QPointF p, double tolerance) const
{
    if (m_points.size() < 2)
        return false;
    QPainterPath path(m_points.first());
    for (int i = 1; i < m_points.size(); ++i)
        path.lineTo(m_points[i]);
    QPainterPathStroker stroker;
    stroker.setWidth(m_style.width + 2 * tolerance);
    return stroker.createStroke(path).contains(p);
}

QPainterPath LineAnnotation::arrowHead(QPointF from, QPointF to, double width)
{
    const QPointF d = to - from;
    const double len = std::hypot(d.x(), d.y());
    if (len < 1e-6)
        return {};
    const QPointF dir = d / len;
    const QPointF normal(-dir.y(), dir.x());
    const double size = std::max(8.0, width * 3.5);
    QPainterPath head;
    head.moveTo(to);
    head.lineTo(to - dir * size + normal * size * 0.5);
    head.lineTo(to - dir * size - normal * size * 0.5);
    head.closeSubpath();
    return head;
}

QString LineAnnotation::headName(Head h)
{
    switch (h) {
    case Head::Triangle: return QStringLiteral("triangle");
    case Head::Open: return QStringLiteral("open");
    case Head::Dot: return QStringLiteral("dot");
    case Head::Bar: return QStringLiteral("bar");
    case Head::None: break;
    }
    return QStringLiteral("none");
}

LineAnnotation::Head LineAnnotation::headFromName(const QString& name)
{
    for (Head h : {Head::Triangle, Head::Open, Head::Dot, Head::Bar})
        if (headName(h) == name)
            return h;
    return Head::None;
}

double LineAnnotation::paintHead(QPainter& p, Head head, QPointF from, QPointF to) const
{
    const QPointF d = to - from;
    const double len = std::hypot(d.x(), d.y());
    if (head == Head::None || len < 1e-6)
        return 0;
    const QPointF dir = d / len;
    const QPointF normal(-dir.y(), dir.x());
    const double size = std::max(8.0, m_style.width * 3.5);
    p.save();
    switch (head) {
    case Head::Triangle:
        p.setPen(Qt::NoPen);
        p.setBrush(m_style.stroke);
        p.drawPath(arrowHead(from, to, m_style.width));
        p.restore();
        return size * 0.6;
    case Head::Open: {
        p.setPen(QPen(m_style.stroke, m_style.width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        const QPointF a = to - dir * size + normal * size * 0.5;
        const QPointF b = to - dir * size - normal * size * 0.5;
        p.drawPolyline(QPolygonF{a, to, b});
        p.restore();
        return 0;
    }
    case Head::Dot: {
        p.setPen(Qt::NoPen);
        p.setBrush(m_style.stroke);
        const double r = std::max(3.0, m_style.width * 1.5);
        p.drawEllipse(to, r, r);
        p.restore();
        return 0;
    }
    case Head::Bar: {
        p.setPen(QPen(m_style.stroke, m_style.width, Qt::SolidLine, Qt::FlatCap));
        p.drawLine(to + normal * size * 0.5, to - normal * size * 0.5);
        p.restore();
        return 0;
    }
    case Head::None:
        break;
    }
    p.restore();
    return 0;
}

void LineAnnotation::paint(QPainter& p, const RenderContext&) const
{
    if (m_points.size() < 2)
        return;
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    // 先画两端箭头，得到线条需要缩短的长度，避免线头露出箭头尖端之外
    const double shortenEnd = paintHead(p, m_headEnd, m_points[m_points.size() - 2], m_points.last());
    const double shortenStart = paintHead(p, m_headStart, m_points[1], m_points.first());
    QList<QPointF> pts = m_points;
    auto shorten = [](QPointF& end, const QPointF& prev, double by) {
        const QPointF d = end - prev;
        const double len = std::hypot(d.x(), d.y());
        if (by > 0 && len > by)
            end = end - d / len * by;
    };
    shorten(pts.last(), pts[pts.size() - 2], shortenEnd);
    shorten(pts.first(), pts[1], shortenStart);
    p.setPen(pen());
    p.setBrush(Qt::NoBrush);
    p.drawPolyline(pts.data(), static_cast<int>(pts.size()));
    p.restore();
}

QJsonObject LineAnnotation::toJson() const
{
    QJsonObject o = Annotation::toJson();
    o[QStringLiteral("points")] = pointsToJson(m_points);
    o[QStringLiteral("headStart")] = headName(m_headStart);
    o[QStringLiteral("headEnd")] = headName(m_headEnd);
    return o;
}

void LineAnnotation::readJson(const QJsonObject& o)
{
    Annotation::readJson(o);
    m_points = pointsFromJson(o.value(QStringLiteral("points")).toArray());
    m_headStart = headFromName(o.value(QStringLiteral("headStart")).toString());
    m_headEnd = headFromName(o.value(QStringLiteral("headEnd")).toString());
    // 兼容旧字段
    if (o.value(QStringLiteral("arrowStart")).toBool())
        m_headStart = Head::Triangle;
    if (o.value(QStringLiteral("arrowEnd")).toBool())
        m_headEnd = Head::Triangle;
}

// ---- 铅笔 ----

QRectF PencilAnnotation::bounds() const
{
    return boundsOfPoints(m_points, m_style.width / 2.0);
}

void PencilAnnotation::setBounds(const QRectF& r)
{
    scalePoints(m_points, bounds(), r);
}

void PencilAnnotation::translate(QPointF d)
{
    for (auto& p : m_points)
        p += d;
}

QPainterPath PencilAnnotation::path() const
{
    return brushPath(m_points, m_style.width, m_smooth);
}

bool PencilAnnotation::hitTest(QPointF p, double tolerance) const
{
    const QPointF local = rotationTransform().inverted().map(p);
    return brushPath(m_points, m_style.width + 2 * tolerance, m_smooth).contains(local);
}

void PencilAnnotation::paint(QPainter& p, const RenderContext&) const
{
    p.save();
    applyRotation(p);
    p.setRenderHint(QPainter::Antialiasing, true);
    if (m_style.penStyle != Qt::SolidLine && m_points.size() > 1) {
        // 虚线 / 点线（ANN-21）：沿中心线描边
        QPainterPath line(m_points.first());
        if (m_smooth && m_points.size() > 2) {
            for (int i = 1; i < m_points.size() - 1; ++i)
                line.quadTo(m_points[i], (m_points[i] + m_points[i + 1]) / 2);
            line.lineTo(m_points.last());
        } else {
            for (int i = 1; i < m_points.size(); ++i)
                line.lineTo(m_points[i]);
        }
        p.setPen(pen());
        p.setBrush(Qt::NoBrush);
        p.drawPath(line);
    } else {
        p.setPen(Qt::NoPen);
        p.setBrush(m_style.stroke);
        p.drawPath(path());
    }
    p.restore();
}

QJsonObject PencilAnnotation::toJson() const
{
    QJsonObject o = Annotation::toJson();
    o[QStringLiteral("points")] = pointsToJson(m_points);
    o[QStringLiteral("smooth")] = m_smooth;
    return o;
}

void PencilAnnotation::readJson(const QJsonObject& o)
{
    Annotation::readJson(o);
    m_points = pointsFromJson(o.value(QStringLiteral("points")).toArray());
    m_smooth = o.value(QStringLiteral("smooth")).toBool();
}

// ---- 荧光笔 ----

QRectF HighlighterAnnotation::bounds() const
{
    return m_shape == Shape::Rect ? m_rect : PencilAnnotation::bounds();
}

void HighlighterAnnotation::setBounds(const QRectF& r)
{
    if (m_shape == Shape::Rect)
        m_rect = r.normalized();
    else
        PencilAnnotation::setBounds(r);
}

void HighlighterAnnotation::translate(QPointF d)
{
    m_rect.translate(d);
    PencilAnnotation::translate(d);
}

bool HighlighterAnnotation::hitTest(QPointF p, double tolerance) const
{
    const QPointF local = rotationTransform().inverted().map(p);
    if (m_shape == Shape::Rect)
        return m_rect.adjusted(-tolerance, -tolerance, tolerance, tolerance).contains(local);
    return PencilAnnotation::hitTest(p, tolerance);
}

void HighlighterAnnotation::paint(QPainter& p, const RenderContext&) const
{
    p.save();
    applyRotation(p);
    p.setRenderHint(QPainter::Antialiasing, true);
    QColor c = m_style.stroke;
    if (m_blend == Blend::Multiply) {
        p.setCompositionMode(QPainter::CompositionMode_Multiply);
        c.setAlpha(255);
    } else if (c.alpha() == 255) {
        c.setAlpha(110);
    }
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    if (m_shape == Shape::Rect)
        p.drawRect(m_rect);
    else
        p.drawPath(brushPath(m_points, m_style.width, m_smooth));
    p.restore();
}

QJsonObject HighlighterAnnotation::toJson() const
{
    QJsonObject o = PencilAnnotation::toJson();
    o[QStringLiteral("shape")] = static_cast<int>(m_shape);
    o[QStringLiteral("blend")] = static_cast<int>(m_blend);
    o[QStringLiteral("rect")] = QJsonArray{m_rect.x(), m_rect.y(), m_rect.width(), m_rect.height()};
    return o;
}

void HighlighterAnnotation::readJson(const QJsonObject& o)
{
    PencilAnnotation::readJson(o);
    m_shape = static_cast<Shape>(o.value(QStringLiteral("shape")).toInt());
    m_blend = static_cast<Blend>(o.value(QStringLiteral("blend")).toInt());
    const QJsonArray r = o.value(QStringLiteral("rect")).toArray();
    if (r.size() == 4)
        m_rect = QRectF(r[0].toDouble(), r[1].toDouble(), r[2].toDouble(), r[3].toDouble());
}

// ---- 马赛克 / 模糊 ----

QPainterPath EffectAnnotation::shapePath() const
{
    QPainterPath path;
    switch (m_shape) {
    case Shape::Rect: path.addRect(m_rect); break;
    case Shape::Ellipse: path.addEllipse(m_rect); break;
    case Shape::Brush: path = brushPath(m_points, m_style.width, false); break;
    }
    return path;
}

QRectF EffectAnnotation::bounds() const
{
    return m_shape == Shape::Brush ? boundsOfPoints(m_points, m_style.width / 2.0) : m_rect;
}

void EffectAnnotation::setBounds(const QRectF& r)
{
    if (m_shape == Shape::Brush)
        scalePoints(m_points, bounds(), r);
    else
        m_rect = r.normalized();
}

void EffectAnnotation::translate(QPointF d)
{
    m_rect.translate(d);
    for (auto& p : m_points)
        p += d;
}

bool EffectAnnotation::hitTest(QPointF p, double tolerance) const
{
    Q_UNUSED(tolerance);
    return shapePath().contains(p);
}

void EffectAnnotation::paint(QPainter& p, const RenderContext& ctx) const
{
    if (!ctx.below)
        return;
    const QRect region = bounds().toAlignedRect().intersected(ctx.below->rect());
    if (region.isEmpty())
        return;
    // 对下层内容的包围区域做效果，再按形状裁剪画回去
    const QImage source = ctx.below->copy(region);
    QImage processed;
    if (m_effect == Effect::Mosaic) {
        const int block = std::max(2, m_strength / 2); // 强度 1~100 → 块 2~50
        processed = fx::mosaic(source, block);
    } else {
        const int radius = std::max(1, m_strength / 2); // 强度 1~100 → 半径 1~50
        processed = fx::gaussianBlur(source, radius);
    }
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setClipPath(shapePath(), Qt::IntersectClip);
    p.drawImage(region.topLeft(), processed);
    p.restore();
}

QJsonObject EffectAnnotation::toJson() const
{
    QJsonObject o = Annotation::toJson();
    o[QStringLiteral("effect")] = static_cast<int>(m_effect);
    o[QStringLiteral("shape")] = static_cast<int>(m_shape);
    o[QStringLiteral("rect")] = QJsonArray{m_rect.x(), m_rect.y(), m_rect.width(), m_rect.height()};
    o[QStringLiteral("points")] = pointsToJson(m_points);
    o[QStringLiteral("strength")] = m_strength;
    return o;
}

void EffectAnnotation::readJson(const QJsonObject& o)
{
    Annotation::readJson(o);
    m_effect = static_cast<Effect>(o.value(QStringLiteral("effect")).toInt());
    m_shape = static_cast<Shape>(o.value(QStringLiteral("shape")).toInt());
    const QJsonArray r = o.value(QStringLiteral("rect")).toArray();
    if (r.size() == 4)
        m_rect = QRectF(r[0].toDouble(), r[1].toDouble(), r[2].toDouble(), r[3].toDouble());
    m_points = pointsFromJson(o.value(QStringLiteral("points")).toArray());
    m_strength = o.value(QStringLiteral("strength")).toInt(30);
}

// ---- 文字 ----

QJsonObject TextDecor::toJson() const
{
    return {{QStringLiteral("outline"), outline}, {QStringLiteral("outlineColor"), outlineColor.isValid() ? outlineColor.name(QColor::HexArgb) : QString()},
            {QStringLiteral("outlineWidth"), outlineWidth}, {QStringLiteral("background"), background},
            {QStringLiteral("backgroundColor"), backgroundColor.name(QColor::HexArgb)}, {QStringLiteral("backgroundRadius"), backgroundRadius},
            {QStringLiteral("backgroundPadding"), backgroundPadding}, {QStringLiteral("shadow"), shadow},
            {QStringLiteral("shadowColor"), shadowColor.name(QColor::HexArgb)}, {QStringLiteral("shadowOffset"), QJsonArray{shadowOffset.x(), shadowOffset.y()}}};
}

TextDecor TextDecor::fromJson(const QJsonObject& o)
{
    TextDecor d;
    d.outline = o.value(QStringLiteral("outline")).toBool();
    d.outlineColor = QColor::fromString(o.value(QStringLiteral("outlineColor")).toString());
    d.outlineWidth = o.value(QStringLiteral("outlineWidth")).toInt(d.outlineWidth);
    d.background = o.value(QStringLiteral("background")).toBool();
    d.backgroundColor = QColor::fromString(o.value(QStringLiteral("backgroundColor")).toString(d.backgroundColor.name(QColor::HexArgb)));
    d.backgroundRadius = o.value(QStringLiteral("backgroundRadius")).toInt(d.backgroundRadius);
    d.backgroundPadding = o.value(QStringLiteral("backgroundPadding")).toInt(d.backgroundPadding);
    d.shadow = o.value(QStringLiteral("shadow")).toBool();
    d.shadowColor = QColor::fromString(o.value(QStringLiteral("shadowColor")).toString(d.shadowColor.name(QColor::HexArgb)));
    const QJsonArray so = o.value(QStringLiteral("shadowOffset")).toArray();
    if (so.size() == 2)
        d.shadowOffset = QPointF(so[0].toDouble(), so[1].toDouble());
    return d;
}

QSizeF TextAnnotation::textSize() const
{
    const QFontMetricsF fm(m_font);
    const QStringList lines = m_text.split(QLatin1Char('\n'));
    double w = 0;
    for (const auto& l : lines)
        w = std::max(w, fm.horizontalAdvance(l));
    return QSizeF(std::max(w, 4.0) + 4 + 2 * padding(), lines.size() * fm.lineSpacing() + 4 + 2 * padding());
}

QRectF TextAnnotation::bounds() const
{
    return QRectF(m_pos, textSize());
}

void TextAnnotation::setBounds(const QRectF& r)
{
    // 按高度比例缩放字号（ANN-32 拖角缩放）
    const QRectF old = bounds();
    if (old.height() > 0) {
        const double factor = r.height() / old.height();
        m_font.setPointSizeF(std::max(4.0, m_font.pointSizeF() * factor));
    }
    m_pos = r.topLeft();
}

bool TextAnnotation::hitTest(QPointF p, double tolerance) const
{
    return Annotation::hitTest(p, tolerance);
}

void TextAnnotation::paint(QPainter& p, const RenderContext&) const
{
    if (m_text.isEmpty())
        return;
    p.save();
    applyRotation(p);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    const QRectF box = bounds();
    if (m_decor.background) {
        p.setPen(Qt::NoPen);
        p.setBrush(m_decor.backgroundColor);
        p.drawRoundedRect(box, m_decor.backgroundRadius, m_decor.backgroundRadius);
    }
    const QRectF textBox = box.adjusted(2 + padding(), 2 + padding(), -2 - padding(), -2 - padding());
    if (!m_decor.outline && !m_decor.shadow) {
        p.setFont(m_font);
        p.setPen(m_style.stroke);
        p.drawText(textBox, Qt::AlignLeft | Qt::AlignTop, m_text);
        p.restore();
        return;
    }
    // 描边 / 投影走路径：先投影，再描边，最后填充（ANN-33）
    QPainterPath path;
    const QFontMetricsF fm(m_font);
    double y = textBox.top() + fm.ascent();
    for (const QString& line : m_text.split(QLatin1Char('\n'))) {
        path.addText(QPointF(textBox.left(), y), m_font, line);
        y += fm.lineSpacing();
    }
    if (m_decor.shadow) {
        p.setPen(Qt::NoPen);
        p.setBrush(m_decor.shadowColor);
        p.drawPath(path.translated(m_decor.shadowOffset));
    }
    if (m_decor.outline) {
        QColor oc = m_decor.outlineColor;
        if (!oc.isValid())
            oc = m_style.stroke.lightnessF() > 0.6 ? QColor(Qt::black) : QColor(Qt::white);
        p.setPen(QPen(oc, m_decor.outlineWidth * 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    }
    p.setPen(Qt::NoPen);
    p.setBrush(m_style.stroke);
    p.drawPath(path);
    p.restore();
}

QJsonObject TextAnnotation::toJson() const
{
    QJsonObject o = Annotation::toJson();
    o[QStringLiteral("text")] = m_text;
    o[QStringLiteral("font")] = m_font.toString();
    o[QStringLiteral("pos")] = QJsonArray{m_pos.x(), m_pos.y()};
    o[QStringLiteral("decor")] = m_decor.toJson();
    return o;
}

void TextAnnotation::readJson(const QJsonObject& o)
{
    Annotation::readJson(o);
    m_text = o.value(QStringLiteral("text")).toString();
    m_font.fromString(o.value(QStringLiteral("font")).toString());
    const QJsonArray p = o.value(QStringLiteral("pos")).toArray();
    if (p.size() == 2)
        m_pos = QPointF(p[0].toDouble(), p[1].toDouble());
    if (o.contains(QStringLiteral("decor")))
        m_decor = TextDecor::fromJson(o.value(QStringLiteral("decor")).toObject());
}

// ---- 橡皮擦 ----

QPainterPath EraserAnnotation::eraseShape() const
{
    QPainterPath path;
    switch (m_shape) {
    case Shape::Rect: path.addRect(m_rect); break;
    case Shape::Ellipse: path.addEllipse(m_rect); break;
    case Shape::Brush: path = brushPath(m_points, m_style.width, false); break;
    }
    return path;
}

QRectF EraserAnnotation::bounds() const
{
    return m_shape == Shape::Brush ? boundsOfPoints(m_points, m_style.width / 2.0) : m_rect;
}

void EraserAnnotation::setBounds(const QRectF& r)
{
    if (m_shape == Shape::Brush)
        scalePoints(m_points, bounds(), r);
    else
        m_rect = r.normalized();
}

void EraserAnnotation::translate(QPointF d)
{
    m_rect.translate(d);
    for (auto& p : m_points)
        p += d;
}

bool EraserAnnotation::hitTest(QPointF p, double tolerance) const
{
    Q_UNUSED(tolerance);
    // 只有矩形 / 椭圆擦除区可二次编辑（ANN-36）
    return m_shape != Shape::Brush && eraseShape().contains(p);
}

QJsonObject EraserAnnotation::toJson() const
{
    QJsonObject o = Annotation::toJson();
    o[QStringLiteral("shape")] = static_cast<int>(m_shape);
    o[QStringLiteral("rect")] = QJsonArray{m_rect.x(), m_rect.y(), m_rect.width(), m_rect.height()};
    o[QStringLiteral("points")] = pointsToJson(m_points);
    return o;
}

void EraserAnnotation::readJson(const QJsonObject& o)
{
    Annotation::readJson(o);
    m_shape = static_cast<Shape>(o.value(QStringLiteral("shape")).toInt());
    const QJsonArray r = o.value(QStringLiteral("rect")).toArray();
    if (r.size() == 4)
        m_rect = QRectF(r[0].toDouble(), r[1].toDouble(), r[2].toDouble(), r[3].toDouble());
    m_points = pointsFromJson(o.value(QStringLiteral("points")).toArray());
}

} // namespace ss
