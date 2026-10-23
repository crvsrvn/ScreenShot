#include "core/annotation/Annotation.h"

#include "core/annotation/Extras.h"
#include "core/annotation/Shapes.h"

#include <QJsonArray>

namespace ss {

QJsonObject AnnotationStyle::toJson() const
{
    return {{QStringLiteral("stroke"), stroke.name(QColor::HexArgb)}, {QStringLiteral("width"), width}, {QStringLiteral("filled"), filled},
            {QStringLiteral("fill"), fill.name(QColor::HexArgb)}, {QStringLiteral("penStyle"), static_cast<int>(penStyle)}};
}

AnnotationStyle AnnotationStyle::fromJson(const QJsonObject& o)
{
    AnnotationStyle s;
    s.stroke = QColor::fromString(o.value(QStringLiteral("stroke")).toString(s.stroke.name(QColor::HexArgb)));
    s.width = o.value(QStringLiteral("width")).toInt(s.width);
    s.filled = o.value(QStringLiteral("filled")).toBool(false);
    s.fill = QColor::fromString(o.value(QStringLiteral("fill")).toString(s.fill.name(QColor::HexArgb)));
    s.penStyle = static_cast<Qt::PenStyle>(o.value(QStringLiteral("penStyle")).toInt(Qt::SolidLine));
    return s;
}

QTransform Annotation::rotationTransform() const
{
    if (qFuzzyIsNull(m_rotation))
        return {};
    const QPointF c = bounds().center();
    QTransform t;
    t.translate(c.x(), c.y());
    t.rotate(m_rotation);
    t.translate(-c.x(), -c.y());
    return t;
}

void Annotation::applyRotation(QPainter& p) const
{
    if (!qFuzzyIsNull(m_rotation))
        p.setTransform(rotationTransform(), true);
}

QRectF Annotation::rotatedBounds() const
{
    const double half = m_style.width / 2.0 + 2;
    return rotationTransform().mapRect(bounds()).adjusted(-half, -half, half, half);
}

bool Annotation::hitTest(QPointF p, double tolerance) const
{
    const QPointF local = rotationTransform().inverted().map(p);
    return bounds().adjusted(-tolerance, -tolerance, tolerance, tolerance).contains(local);
}

QPen Annotation::pen() const
{
    QPen pen(m_style.stroke, m_style.width, m_style.penStyle, Qt::RoundCap, Qt::RoundJoin);
    return pen;
}

QJsonObject Annotation::toJson() const
{
    return {{QStringLiteral("type"), type()}, {QStringLiteral("id"), m_id}, {QStringLiteral("rotation"), m_rotation}, {QStringLiteral("style"), m_style.toJson()}};
}

void Annotation::readJson(const QJsonObject& o)
{
    m_id = o.value(QStringLiteral("id")).toInt();
    m_rotation = o.value(QStringLiteral("rotation")).toDouble();
    m_style = AnnotationStyle::fromJson(o.value(QStringLiteral("style")).toObject());
}

std::unique_ptr<Annotation> createAnnotation(const QString& type)
{
    if (type == QLatin1String("rect"))
        return std::make_unique<RectAnnotation>();
    if (type == QLatin1String("ellipse"))
        return std::make_unique<EllipseAnnotation>();
    if (type == QLatin1String("line"))
        return std::make_unique<LineAnnotation>();
    if (type == QLatin1String("pencil"))
        return std::make_unique<PencilAnnotation>();
    if (type == QLatin1String("highlighter"))
        return std::make_unique<HighlighterAnnotation>();
    if (type == QLatin1String("effect"))
        return std::make_unique<EffectAnnotation>();
    if (type == QLatin1String("text"))
        return std::make_unique<TextAnnotation>();
    if (type == QLatin1String("eraser"))
        return std::make_unique<EraserAnnotation>();
    if (type == QLatin1String("step"))
        return std::make_unique<StepAnnotation>();
    if (type == QLatin1String("magnifier"))
        return std::make_unique<MagnifierAnnotation>();
    return nullptr;
}

std::unique_ptr<Annotation> annotationFromJson(const QJsonObject& o)
{
    auto a = createAnnotation(o.value(QStringLiteral("type")).toString());
    if (a)
        a->readJson(o);
    return a;
}

} // namespace ss
