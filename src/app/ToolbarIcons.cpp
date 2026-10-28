#include "app/ToolbarIcons.h"

#include "app/Theme.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QtMath>

namespace ss {

namespace {

// 画在 20×20 的逻辑坐标里；painter 已设好画笔（2px、圆头）与颜色
using Draw = void (*)(QPainter&, const QColor&);

void drawRect(QPainter& p, const QColor&) { p.drawRect(QRectF(3, 4, 14, 12)); }
void drawEllipse(QPainter& p, const QColor&) { p.drawEllipse(QRectF(3, 4, 14, 12)); }
void drawLine(QPainter& p, const QColor&) { p.drawLine(QPointF(3, 16), QPointF(17, 4)); }
void drawArrow(QPainter& p, const QColor&)
{
    p.drawLine(QPointF(3, 17), QPointF(16, 4));
    p.drawLine(QPointF(16, 4), QPointF(9, 5));
    p.drawLine(QPointF(16, 4), QPointF(15, 11));
}
void drawPencil(QPainter& p, const QColor&)
{
    p.drawLine(QPointF(4, 16), QPointF(14, 6));
    p.drawLine(QPointF(14, 6), QPointF(16, 8));
    p.drawLine(QPointF(16, 8), QPointF(6, 18));
    p.drawLine(QPointF(6, 18), QPointF(3, 19));
    p.drawLine(QPointF(3, 19), QPointF(4, 16));
}
void drawHighlighter(QPainter& p, const QColor& c)
{
    QColor fill = c;
    fill.setAlphaF(0.45f);
    p.fillRect(QRectF(3, 8, 14, 6), fill);
    p.drawLine(QPointF(3, 16), QPointF(17, 16));
}
void drawMosaic(QPainter& p, const QColor& c)
{
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x)
            if ((x + y) % 2 == 0)
                p.fillRect(QRectF(3 + x * 4.7, 3 + y * 4.7, 4.7, 4.7), c);
}
void drawBlur(QPainter& p, const QColor& c)
{
    for (int i = 0; i < 3; ++i) {
        QColor ring = c;
        ring.setAlphaF(1.0f - i * 0.3f);
        p.setPen(QPen(ring, 2));
        p.drawEllipse(QPointF(10, 10), 3.0 + i * 2.6, 3.0 + i * 2.6);
    }
}
void drawText(QPainter& p, const QColor&)
{
    p.drawLine(QPointF(4, 4), QPointF(16, 4));
    p.drawLine(QPointF(10, 4), QPointF(10, 17));
    p.drawLine(QPointF(7, 17), QPointF(13, 17));
}
void drawEraser(QPainter& p, const QColor&)
{
    p.save();
    p.translate(10, 10);
    p.rotate(-45);
    p.drawRect(QRectF(-7, -4, 14, 8));
    p.drawLine(QPointF(-2, -4), QPointF(-2, 4));
    p.restore();
}
void drawStep(QPainter& p, const QColor&)
{
    p.drawEllipse(QPointF(10, 10), 7, 7);
    p.drawLine(QPointF(10, 6), QPointF(10, 14));
    p.drawLine(QPointF(8, 8), QPointF(10, 6));
}
void drawMagnifier(QPainter& p, const QColor&)
{
    p.drawEllipse(QPointF(8.5, 8.5), 5.5, 5.5);
    p.drawLine(QPointF(12.5, 12.5), QPointF(17, 17));
}
void drawSpotlight(QPainter& p, const QColor& c)
{
    QColor dim = c;
    dim.setAlphaF(0.35f);
    p.fillRect(QRectF(2, 2, 16, 16), dim);
    p.setBrush(c);
    p.setPen(Qt::NoPen);
    p.drawEllipse(QPointF(10, 10), 4.5, 4.5);
}
void drawSmartErase(QPainter& p, const QColor&)
{
    p.drawRect(QRectF(3, 3, 14, 14));
    p.setPen(QPen(p.pen().color(), 2, Qt::DotLine));
    p.drawRect(QRectF(7, 7, 6, 6));
}
void drawMore(QPainter& p, const QColor& c)
{
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    for (int i = 0; i < 3; ++i)
        p.drawEllipse(QPointF(5 + i * 5, 10), 1.6, 1.6);
}
void drawClear(QPainter& p, const QColor&)
{
    p.drawLine(QPointF(4, 6), QPointF(16, 6));
    p.drawLine(QPointF(8, 3), QPointF(12, 3));
    p.drawLine(QPointF(6, 6), QPointF(7, 17));
    p.drawLine(QPointF(14, 6), QPointF(13, 17));
    p.drawLine(QPointF(7, 17), QPointF(13, 17));
}
void drawCopy(QPainter& p, const QColor&)
{
    p.drawRect(QRectF(3, 3, 10, 11));
    p.drawRect(QRectF(7, 7, 10, 11));
}
void drawSave(QPainter& p, const QColor& c)
{
    p.drawRect(QRectF(3, 3, 14, 14));
    p.fillRect(QRectF(6, 3, 8, 5), c);
    p.drawRect(QRectF(6, 11, 8, 6));
}
void drawPin(QPainter& p, const QColor& c)
{
    p.setBrush(c);
    p.drawEllipse(QPointF(10, 7), 4, 4);
    p.drawLine(QPointF(10, 11), QPointF(10, 18));
}
void drawCodes(QPainter& p, const QColor& c)
{
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawRect(QRectF(3, 3, 6, 6));
    p.drawRect(QRectF(11, 3, 6, 6));
    p.drawRect(QRectF(3, 11, 6, 6));
    p.drawRect(QRectF(12, 12, 2, 2));
    p.drawRect(QRectF(15, 15, 2, 2));
    p.drawRect(QRectF(12, 15, 2, 2));
}
void drawModeCapture(QPainter& p, const QColor&)
{
    p.drawRect(QRectF(3, 6, 14, 10));
    p.drawEllipse(QPointF(10, 11), 3, 3);
    p.drawLine(QPointF(7, 6), QPointF(8, 4));
    p.drawLine(QPointF(8, 4), QPointF(12, 4));
    p.drawLine(QPointF(12, 4), QPointF(13, 6));
}
void drawScroll(QPainter& p, const QColor&)
{
    p.drawRect(QRectF(4, 3, 12, 14));
    p.drawLine(QPointF(10, 6), QPointF(10, 14));
    p.drawLine(QPointF(7, 9), QPointF(10, 6));
    p.drawLine(QPointF(13, 9), QPointF(10, 6));
    p.drawLine(QPointF(7, 11), QPointF(10, 14));
    p.drawLine(QPointF(13, 11), QPointF(10, 14));
}
void drawRecord(QPainter& p, const QColor&)
{
    p.drawEllipse(QPointF(10, 10), 7, 7);
    p.setBrush(QColor(230, 50, 50));
    p.setPen(Qt::NoPen);
    p.drawEllipse(QPointF(10, 10), 3.5, 3.5);
}
void drawRatio(QPainter& p, const QColor&)
{
    p.drawRect(QRectF(3, 3, 14, 14));
    p.drawLine(QPointF(3, 17), QPointF(17, 3));
}
void drawShadow(QPainter& p, const QColor& c)
{
    QColor faint = c;
    faint.setAlphaF(0.4f);
    p.fillRect(QRectF(6, 6, 11, 11), faint);
    p.setBrush(theme::colors().surface[0]);
    p.drawRect(QRectF(3, 3, 11, 11));
}
void drawBorder(QPainter& p, const QColor&)
{
    p.drawRect(QRectF(3, 3, 14, 14));
    p.setPen(QPen(p.pen().color(), 1));
    p.drawRect(QRectF(6, 6, 8, 8));
}
void drawCancel(QPainter& p, const QColor&)
{
    p.drawLine(QPointF(4, 4), QPointF(16, 16));
    p.drawLine(QPointF(16, 4), QPointF(4, 16));
}
void drawRecent(QPainter& p, const QColor&)
{
    p.drawEllipse(QPointF(10, 10), 7, 7);
    p.drawLine(QPointF(10, 6), QPointF(10, 10));
    p.drawLine(QPointF(10, 10), QPointF(13, 12));
}

struct Entry {
    const char* name;
    Draw draw;
};

const Entry kEntries[] = {
    {"rect", drawRect}, {"ellipse", drawEllipse}, {"line", drawLine}, {"arrow", drawArrow}, {"pencil", drawPencil},
    {"highlighter", drawHighlighter}, {"mosaic", drawMosaic}, {"blur", drawBlur}, {"text", drawText}, {"eraser", drawEraser},
    {"step", drawStep}, {"magnifier", drawMagnifier}, {"spotlight", drawSpotlight}, {"smartErase", drawSmartErase},
    {"more", drawMore}, {"recent", drawRecent}, {"clear", drawClear},
    {"copy", drawCopy}, {"save", drawSave}, {"pin", drawPin}, {"codes", drawCodes},
    {"mode", drawModeCapture}, {"modeStatic", drawModeCapture}, {"modeScroll", drawScroll}, {"modeRecord", drawRecord},
    {"scroll", drawScroll}, {"ratio", drawRatio}, {"shadow", drawShadow}, {"border", drawBorder}, {"cancel", drawCancel},
};

Draw drawFor(const QString& name)
{
    for (const Entry& e : kEntries)
        if (name == QLatin1String(e.name))
            return e.draw;
    return nullptr;
}

} // namespace

bool hasToolbarIcon(const QString& name)
{
    return drawFor(name) != nullptr;
}

QIcon toolbarIcon(const QString& name, const QColor& requested)
{
    const Draw draw = drawFor(name);
    if (!draw)
        return {};
    // 未指定颜色时：常态用控件文字色，选中态（金色按钮上）用主按钮文字色
    const QColor off = requested.isValid() ? requested : theme::colors().controlText;
    const QColor on = requested.isValid() ? requested : theme::colors().onAccent;
    QIcon icon;
    for (const auto& [color, state] : {std::pair{off, QIcon::Off}, std::pair{on, QIcon::On}}) {
        for (int scale : {1, 2, 3}) { // 高分屏（NFR-07）：按整数倍各画一份
            QPixmap pm(20 * scale, 20 * scale);
            pm.fill(Qt::transparent);
            pm.setDevicePixelRatio(scale);
            QPainter p(&pm);
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(QPen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)); // Fluent 图标的细线
            p.setBrush(Qt::NoBrush);
            draw(p, color);
            icon.addPixmap(pm, QIcon::Normal, state);
        }
    }
    return icon;
}

} // namespace ss
