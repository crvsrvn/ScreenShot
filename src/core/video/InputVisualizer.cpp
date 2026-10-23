#include "core/video/InputVisualizer.h"

#include <QFontMetrics>

namespace ss {

void paintInputEvents(QPainter& p, const QList<InputEvent>& events, qint64 t, const QSize& frameSize, const InputVisualOptions& o)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    QStringList keys;
    for (const InputEvent& e : events) {
        const qint64 age = t - e.timeMs;
        if (age < 0)
            continue;
        if (e.kind == InputEvent::Kind::Click && o.showClicks && age <= o.clickDurationMs) {
            // 涟漪：半径随时间扩大、透明度减小；按键颜色区分
            const double k = static_cast<double>(age) / o.clickDurationMs;
            const QColor c = e.button == 1 ? QColor(255, 200, 0) : e.button == 2 ? QColor(60, 190, 90) : QColor(60, 140, 255);
            QColor stroke = c;
            stroke.setAlphaF(1.0 - k);
            p.setPen(QPen(stroke, 3));
            QColor fill = c;
            fill.setAlphaF((1.0 - k) * 0.35);
            p.setBrush(fill);
            const double r = o.radius * (0.4 + 0.6 * k);
            p.drawEllipse(QPointF(e.pos), r, r);
        } else if (e.kind == InputEvent::Kind::Wheel && o.showWheel && age <= o.clickDurationMs) {
            const double k = static_cast<double>(age) / o.clickDurationMs;
            QColor c(255, 255, 255);
            c.setAlphaF(1.0 - k);
            p.setPen(QPen(c, 3, Qt::SolidLine, Qt::RoundCap));
            p.setBrush(Qt::NoBrush);
            const double dir = e.wheel > 0 ? -1 : 1; // 上滚：箭头向上
            const QPointF c0(e.pos);
            const QPointF tip = c0 + QPointF(0, dir * o.radius);
            p.drawLine(c0 - QPointF(0, dir * o.radius * 0.6), tip);
            p.drawLine(tip, tip + QPointF(-8, -dir * 8));
            p.drawLine(tip, tip + QPointF(8, -dir * 8));
        } else if (e.kind == InputEvent::Kind::Key && o.showKeys && age <= o.keyDurationMs) {
            keys.append(e.keyText);
        }
    }
    if (!keys.isEmpty()) {
        // 按键标签：底部居中的圆角条
        QFont f = p.font();
        f.setPointSize(14);
        f.setBold(true);
        p.setFont(f);
        const QString text = keys.join(QStringLiteral("  "));
        const QFontMetrics fm(f);
        const QRectF box((frameSize.width() - fm.horizontalAdvance(text) - 24) / 2.0, frameSize.height() - fm.height() - 36, fm.horizontalAdvance(text) + 24, fm.height() + 12);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 170));
        p.drawRoundedRect(box, 8, 8);
        p.setPen(Qt::white);
        p.drawText(box, Qt::AlignCenter, text);
    }
    p.restore();
}

QImage renderWithInputEvents(const QImage& frame, const QList<InputEvent>& events, qint64 frameTimeMs, const InputVisualOptions& options)
{
    QImage out = frame.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QPainter p(&out);
    paintInputEvents(p, events, frameTimeMs, out.size(), options);
    return out;
}

} // namespace ss
