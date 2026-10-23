#include "core/annotation/Extras.h"

#include "core/annotation/Shapes.h"

#include <QFontMetricsF>
#include <QJsonArray>
#include <QPainterPathStroker>
#include <cmath>

namespace ss {

namespace {

QRectF rectFromJson(const QJsonArray& r)
{
    return r.size() == 4 ? QRectF(r[0].toDouble(), r[1].toDouble(), r[2].toDouble(), r[3].toDouble()) : QRectF();
}

QJsonArray rectToJson(const QRectF& r)
{
    return {r.x(), r.y(), r.width(), r.height()};
}

// 深色背景上用白字，浅色上用黑字
QColor contrastColor(const QColor& c)
{
    return c.lightnessF() > 0.6 ? QColor(Qt::black) : QColor(Qt::white);
}

} // namespace

// ---- 序号 ----

QString StepAnnotation::formatNumber(int n, NumberStyle style)
{
    n = std::max(1, n);
    switch (style) {
    case NumberStyle::Numeric:
        return QString::number(n);
    case NumberStyle::Alpha: {
        QString s;
        for (int v = n; v > 0; v = (v - 1) / 26)
            s.prepend(QChar(QLatin1Char('A').toLatin1() + (v - 1) % 26));
        return s;
    }
    case NumberStyle::Roman: {
        static const std::pair<int, const char*> table[] = {{1000, "M"}, {900, "CM"}, {500, "D"}, {400, "CD"}, {100, "C"}, {90, "XC"}, {50, "L"},
                                                            {40, "XL"}, {10, "X"}, {9, "IX"}, {5, "V"}, {4, "IV"}, {1, "I"}};
        QString s;
        int v = std::min(n, 3999);
        for (const auto& [value, sym] : table)
            while (v >= value) {
                s += QLatin1String(sym);
                v -= value;
            }
        return s;
    }
    }
    return QString::number(n);
}

QString StepAnnotation::label() const
{
    return formatNumber(m_number, m_numberStyle);
}

double StepAnnotation::radius() const
{
    return 12.0 + m_style.width * 2.0;
}

QRectF StepAnnotation::badgeRect() const
{
    const double r = radius();
    return QRectF(m_center.x() - r, m_center.y() - r, 2 * r, 2 * r);
}

QRectF StepAnnotation::textRect() const
{
    if (m_text.isEmpty())
        return {};
    const QFontMetricsF fm(m_font);
    const QRectF br = fm.boundingRect(QRectF(0, 0, 10000, 10000), Qt::AlignLeft | Qt::AlignTop, m_text);
    const double r = radius();
    return QRectF(m_center.x() + r + 6, m_center.y() - br.height() / 2, br.width() + 4, br.height());
}

QRectF StepAnnotation::bounds() const
{
    QRectF b = badgeRect();
    if (m_tail)
        b = b.united(QRectF(*m_tail, QSizeF(1, 1)).adjusted(-m_style.width, -m_style.width, m_style.width, m_style.width));
    if (!m_text.isEmpty())
        b = b.united(textRect());
    return b;
}

void StepAnnotation::setBounds(const QRectF& r)
{
    translate(r.topLeft() - bounds().topLeft());
}

void StepAnnotation::translate(QPointF d)
{
    m_center += d;
    if (m_tail)
        *m_tail += d;
}

bool StepAnnotation::hitTest(QPointF p, double tolerance) const
{
    if (badgeRect().adjusted(-tolerance, -tolerance, tolerance, tolerance).contains(p))
        return true;
    if (!m_text.isEmpty() && textRect().adjusted(-tolerance, -tolerance, tolerance, tolerance).contains(p))
        return true;
    if (m_tail) {
        QPainterPath line(m_center);
        line.lineTo(*m_tail);
        QPainterPathStroker stroker;
        stroker.setWidth(m_style.width + 2 * tolerance);
        return stroker.createStroke(line).contains(p);
    }
    return false;
}

void StepAnnotation::paint(QPainter& p, const RenderContext&) const
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    if (m_tail) {
        // 指示箭头：从徽标边缘指向目标点
        const QPointF d = *m_tail - m_center;
        const double len = std::hypot(d.x(), d.y());
        if (len > radius()) {
            const QPointF from = m_center + d / len * radius();
            p.setPen(QPen(m_style.stroke, m_style.width, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(from, *m_tail);
            p.setPen(Qt::NoPen);
            p.setBrush(m_style.stroke);
            p.drawPath(LineAnnotation::arrowHead(from, *m_tail, m_style.width));
        }
    }
    p.setPen(QPen(contrastColor(m_style.stroke), 1.0));
    p.setBrush(m_style.stroke);
    p.drawEllipse(badgeRect());
    QFont f = m_font;
    f.setBold(true);
    f.setPixelSize(static_cast<int>(std::round(radius() * (label().size() > 2 ? 0.8 : 1.1))));
    p.setFont(f);
    p.setPen(contrastColor(m_style.stroke));
    p.drawText(badgeRect(), Qt::AlignCenter, label());
    if (!m_text.isEmpty()) {
        // 说明文字：描边保证在任何背景上可读
        QPainterPath path;
        const QFontMetricsF fm(m_font);
        const QRectF tr = textRect();
        double y = tr.top() + fm.ascent();
        for (const QString& line : m_text.split(QLatin1Char('\n'))) {
            path.addText(QPointF(tr.left() + 2, y), m_font, line);
            y += fm.lineSpacing();
        }
        p.setPen(QPen(contrastColor(m_style.stroke), 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
        p.setPen(Qt::NoPen);
        p.setBrush(m_style.stroke);
        p.drawPath(path);
    }
    p.restore();
}

QJsonObject StepAnnotation::toJson() const
{
    QJsonObject o = Annotation::toJson();
    o[QStringLiteral("center")] = QJsonArray{m_center.x(), m_center.y()};
    o[QStringLiteral("number")] = m_number;
    o[QStringLiteral("numberStyle")] = static_cast<int>(m_numberStyle);
    if (m_tail)
        o[QStringLiteral("tail")] = QJsonArray{m_tail->x(), m_tail->y()};
    o[QStringLiteral("text")] = m_text;
    o[QStringLiteral("font")] = m_font.toString();
    return o;
}

void StepAnnotation::readJson(const QJsonObject& o)
{
    Annotation::readJson(o);
    const QJsonArray c = o.value(QStringLiteral("center")).toArray();
    if (c.size() == 2)
        m_center = QPointF(c[0].toDouble(), c[1].toDouble());
    m_number = o.value(QStringLiteral("number")).toInt(1);
    m_numberStyle = static_cast<NumberStyle>(o.value(QStringLiteral("numberStyle")).toInt());
    const QJsonArray t = o.value(QStringLiteral("tail")).toArray();
    m_tail = t.size() == 2 ? std::optional<QPointF>(QPointF(t[0].toDouble(), t[1].toDouble())) : std::nullopt;
    m_text = o.value(QStringLiteral("text")).toString();
    m_font.fromString(o.value(QStringLiteral("font")).toString());
}

// ---- 放大 ----

QPainterPath MagnifierAnnotation::targetPath() const
{
    QPainterPath path;
    if (m_shape == Shape::Ellipse)
        path.addEllipse(m_target);
    else
        path.addRect(m_target);
    return path;
}

void MagnifierAnnotation::setBounds(const QRectF& r)
{
    m_target = r.normalized();
}

void MagnifierAnnotation::placeTarget(double zoom, const QRectF& canvas)
{
    zoom = std::clamp(zoom, 1.0, 8.0);
    const QSizeF size(m_source.width() * zoom, m_source.height() * zoom);
    const double gap = 16;
    QRectF t(QPointF(m_source.right() + gap, m_source.top()), size);
    if (!canvas.isEmpty()) {
        if (t.right() > canvas.right())
            t.moveLeft(m_source.left() - gap - size.width()); // 右侧放不下：左侧
        if (t.left() < canvas.left()) // 两侧都放不下：下方
            t.moveTopLeft(QPointF(std::max(canvas.left(), std::min(m_source.left(), canvas.right() - size.width())), m_source.bottom() + gap));
        if (t.bottom() > canvas.bottom())
            t.moveTop(std::max(canvas.top(), canvas.bottom() - size.height()));
    }
    m_target = t;
}

bool MagnifierAnnotation::hitTest(QPointF p, double tolerance) const
{
    const double t = tolerance + m_style.width / 2.0;
    QPainterPath outer;
    if (m_shape == Shape::Ellipse)
        outer.addEllipse(m_target.adjusted(-t, -t, t, t));
    else
        outer.addRect(m_target.adjusted(-t, -t, t, t));
    return outer.contains(p);
}

void MagnifierAnnotation::paint(QPainter& p, const RenderContext& ctx) const
{
    const QImage* src = m_includeAnnotations ? ctx.below : (ctx.base ? ctx.base : ctx.below);
    if (!src || m_source.isEmpty() || m_target.isEmpty())
        return;
    // 先复制源区域：源可能就是当前画布，必须在画连接线 / 轮廓之前取样
    const QRect region = m_source.toAlignedRect().intersected(src->rect());
    const QImage part = region.isEmpty() ? QImage() : src->copy(region);
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    // 连接线：源中心 → 目标中心，画在两个形状下面
    QPen link(m_style.stroke, std::max(1.0, m_style.width / 2.0), m_style.penStyle, Qt::RoundCap);
    p.setPen(link);
    p.drawLine(m_source.center(), m_target.center());
    // 源区域轮廓
    p.setPen(QPen(m_style.stroke, 1.0));
    p.setBrush(Qt::NoBrush);
    if (m_shape == Shape::Ellipse)
        p.drawEllipse(m_source);
    else
        p.drawRect(m_source);
    // 阴影：几层半透明、逐渐外扩的轮廓
    if (m_shadow) {
        p.setPen(Qt::NoPen);
        for (int i = 4; i >= 1; --i) {
            p.setBrush(QColor(0, 0, 0, 22));
            QPainterPath sp;
            const QRectF r = m_target.adjusted(-i, -i + 3, i, i + 3);
            if (m_shape == Shape::Ellipse)
                sp.addEllipse(r);
            else
                sp.addRect(r);
            p.drawPath(sp);
        }
    }
    // 放大的内容
    if (!part.isNull()) {
        p.save();
        p.setClipPath(targetPath(), Qt::IntersectClip);
        const double sx = m_target.width() / m_source.width();
        const double sy = m_target.height() / m_source.height();
        const QRectF dest(m_target.left() + (region.left() - m_source.left()) * sx, m_target.top() + (region.top() - m_source.top()) * sy,
                          region.width() * sx, region.height() * sy);
        p.drawImage(dest, part, QRectF(QPointF(0, 0), QSizeF(region.size())));
        p.restore();
    }
    // 边框
    p.setPen(QPen(m_style.stroke, m_style.width, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPath(targetPath());
    p.restore();
}

QJsonObject MagnifierAnnotation::toJson() const
{
    QJsonObject o = Annotation::toJson();
    o[QStringLiteral("source")] = rectToJson(m_source);
    o[QStringLiteral("target")] = rectToJson(m_target);
    o[QStringLiteral("shape")] = static_cast<int>(m_shape);
    o[QStringLiteral("includeAnnotations")] = m_includeAnnotations;
    o[QStringLiteral("shadow")] = m_shadow;
    return o;
}

void MagnifierAnnotation::readJson(const QJsonObject& o)
{
    Annotation::readJson(o);
    m_source = rectFromJson(o.value(QStringLiteral("source")).toArray());
    m_target = rectFromJson(o.value(QStringLiteral("target")).toArray());
    m_shape = static_cast<Shape>(o.value(QStringLiteral("shape")).toInt());
    m_includeAnnotations = o.value(QStringLiteral("includeAnnotations")).toBool();
    m_shadow = o.value(QStringLiteral("shadow")).toBool(true);
}

} // namespace ss
