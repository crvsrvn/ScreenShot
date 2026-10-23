#include "core/output/Decorate.h"

#include "core/fx/Blur.h"

#include <QPainter>
#include <QPainterPath>

namespace ss {

QImage decorateImage(const QImage& sourceIn, const DecorateOptions& o)
{
    if (!o.any())
        return sourceIn;
    const QImage source = sourceIn.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const int m = o.shadowMargin();
    QImage out(source.width() + 2 * m, source.height() + 2 * m, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainterPath shape;
    const QRectF content(m, m, source.width(), source.height());
    if (o.cornerRadius > 0)
        shape.addRoundedRect(content, o.cornerRadius, o.cornerRadius);
    else
        shape.addRect(content);

    if (o.shadow) {
        QImage shadow(out.size(), QImage::Format_ARGB32_Premultiplied);
        shadow.fill(Qt::transparent);
        {
            QPainter sp(&shadow);
            sp.setRenderHint(QPainter::Antialiasing);
            sp.translate(0, o.shadowOffsetY);
            sp.fillPath(shape, o.shadowColor);
        }
        shadow = fx::gaussianBlur(shadow, o.shadowRadius);
        QPainter p(&out);
        p.drawImage(0, 0, shadow);
    }
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing, o.cornerRadius > 0);
    p.setClipPath(shape);
    p.drawImage(content.topLeft(), source);
    p.setClipping(false);
    if (o.border) {
        QPen pen(o.borderColor, o.borderWidth);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.setClipPath(shape); // 边框画在内侧，不改变尺寸
        p.setRenderHint(QPainter::Antialiasing, true);
        p.drawPath(shape);
    }
    p.end();
    return out.convertToFormat(QImage::Format_ARGB32);
}

QImage composeCursor(const QImage& source, const QImage& cursor, QPoint cursorTopLeft)
{
    if (cursor.isNull())
        return source;
    QImage out = source.convertToFormat(QImage::Format_ARGB32);
    QPainter p(&out);
    p.drawImage(cursorTopLeft, cursor);
    return out;
}

} // namespace ss
