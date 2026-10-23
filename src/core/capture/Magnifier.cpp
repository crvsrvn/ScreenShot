#include "core/capture/Magnifier.h"

#include <QCoreApplication>
#include <QFontMetrics>

namespace ss {

namespace {
constexpr int kTextRows = 3; // 坐标 / 尺寸、颜色、提示
constexpr int kRowHeight = 18;
constexpr int kPadding = 6;
} // namespace

QSize magnifierSize(const MagnifierOptions& o)
{
    const int side = o.zoom * o.pixels;
    const int rows = o.showHint ? kTextRows : kTextRows - 1;
    return QSize(side + 2 * kPadding, side + 2 * kPadding + rows * kRowHeight);
}

QPointF magnifierPlacement(QPointF cursor, QSize panel, const QRectF& screen, int offset)
{
    QPointF p = cursor + QPointF(offset, offset);
    if (p.x() + panel.width() > screen.right())
        p.setX(cursor.x() - offset - panel.width());
    if (p.y() + panel.height() > screen.bottom())
        p.setY(cursor.y() - offset - panel.height());
    p.setX(std::max(p.x(), screen.left()));
    p.setY(std::max(p.y(), screen.top()));
    return p;
}

void paintMagnifier(QPainter& p, QPointF topLeft, const VirtualDesktop& desktop, QPoint cursor, const QRect& selection,
                    const MagnifierOptions& o, const QString& colorText)
{
    const QSize size = magnifierSize(o);
    const QRectF panel(topLeft, QSizeF(size));
    p.save();
    p.setRenderHint(QPainter::Antialiasing, false);
    p.fillRect(panel, QColor(30, 30, 30, 230));

    // 放大区域：从虚拟桌面裁出 pixels×pixels 的像素块，最近邻放大
    const int half = o.pixels / 2;
    const QRect src(cursor.x() - half, cursor.y() - half, o.pixels, o.pixels);
    const QImage block = desktop.crop(src);
    const QRectF zoomRect(topLeft + QPointF(kPadding, kPadding), QSizeF(o.zoom * o.pixels, o.zoom * o.pixels));
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    p.drawImage(zoomRect, block);
    if (o.grid) {
        QPen grid(QColor(255, 255, 255, 40));
        grid.setCosmetic(true);
        p.setPen(grid);
        for (int i = 1; i < o.pixels; ++i) {
            const qreal x = zoomRect.left() + i * o.zoom;
            const qreal y = zoomRect.top() + i * o.zoom;
            p.drawLine(QPointF(x, zoomRect.top()), QPointF(x, zoomRect.bottom()));
            p.drawLine(QPointF(zoomRect.left(), y), QPointF(zoomRect.right(), y));
        }
    }
    // 中心像素十字线
    QPen cross(QColor(40, 120, 220));
    cross.setCosmetic(true);
    p.setPen(cross);
    const QRectF center(zoomRect.left() + half * o.zoom, zoomRect.top() + half * o.zoom, o.zoom, o.zoom);
    p.drawRect(center);
    p.setPen(QColor(40, 120, 220, 120));
    p.drawLine(QPointF(zoomRect.left(), center.center().y()), QPointF(center.left(), center.center().y()));
    p.drawLine(QPointF(center.right(), center.center().y()), QPointF(zoomRect.right(), center.center().y()));
    p.drawLine(QPointF(center.center().x(), zoomRect.top()), QPointF(center.center().x(), center.top()));
    p.drawLine(QPointF(center.center().x(), center.bottom()), QPointF(center.center().x(), zoomRect.bottom()));

    // 文本行
    p.setPen(Qt::white);
    QFont f = p.font();
    f.setPointSize(9);
    p.setFont(f);
    qreal y = zoomRect.bottom() + kPadding;
    auto row = [&](const QString& text) {
        p.drawText(QRectF(topLeft.x() + kPadding, y, size.width() - 2 * kPadding, kRowHeight), Qt::AlignVCenter | Qt::AlignLeft, text);
        y += kRowHeight;
    };
    QString first = QStringLiteral("(%1, %2)").arg(cursor.x()).arg(cursor.y());
    if (!selection.isEmpty())
        first += QStringLiteral("  %1 × %2").arg(selection.width()).arg(selection.height());
    row(first);
    row(colorText);
    if (o.showHint)
        row(QCoreApplication::translate("Magnifier", "C: copy color   Shift: format"));
    p.restore();
}

} // namespace ss
