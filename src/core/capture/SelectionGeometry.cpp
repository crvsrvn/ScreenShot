#include "core/capture/SelectionGeometry.h"

#include <cstdlib>

namespace ss {

QList<QPoint> selectionHandlePoints(const QRect& s)
{
    const int cx = s.left() + s.width() / 2;
    const int cy = s.top() + s.height() / 2;
    return {QPoint(s.left(), s.top()), QPoint(cx, s.top()), QPoint(s.right(), s.top()), QPoint(s.right(), cy),
            QPoint(s.right(), s.bottom()), QPoint(cx, s.bottom()), QPoint(s.left(), s.bottom()), QPoint(s.left(), cy)};
}

SelectionHandle hitTestSelection(const QRect& selection, QPoint p, int grip)
{
    if (selection.isEmpty())
        return SelectionHandle::Outside;
    const QList<QPoint> points = selectionHandlePoints(selection);
    static const SelectionHandle handles[] = {SelectionHandle::TopLeft, SelectionHandle::Top, SelectionHandle::TopRight, SelectionHandle::Right,
                                              SelectionHandle::BottomRight, SelectionHandle::Bottom, SelectionHandle::BottomLeft, SelectionHandle::Left};
    for (int i = 0; i < 8; ++i) {
        if (std::abs(points[i].x() - p.x()) <= grip && std::abs(points[i].y() - p.y()) <= grip)
            return handles[i];
    }
    // 边线附近也算锚点（便于细长选区）
    const QRect outer = selection.adjusted(-grip, -grip, grip, grip);
    const QRect inner = selection.adjusted(grip, grip, -grip, -grip);
    if (outer.contains(p) && !inner.contains(p)) {
        if (p.y() < selection.top() + grip)
            return SelectionHandle::Top;
        if (p.y() > selection.bottom() - grip)
            return SelectionHandle::Bottom;
        if (p.x() < selection.left() + grip)
            return SelectionHandle::Left;
        return SelectionHandle::Right;
    }
    return selection.contains(p) ? SelectionHandle::Inside : SelectionHandle::Outside;
}

QRect resizeSelection(const QRect& o, SelectionHandle h, QPoint d)
{
    int l = o.left(), t = o.top(), r = o.right(), b = o.bottom();
    switch (h) {
    case SelectionHandle::TopLeft: l += d.x(); t += d.y(); break;
    case SelectionHandle::Top: t += d.y(); break;
    case SelectionHandle::TopRight: r += d.x(); t += d.y(); break;
    case SelectionHandle::Right: r += d.x(); break;
    case SelectionHandle::BottomRight: r += d.x(); b += d.y(); break;
    case SelectionHandle::Bottom: b += d.y(); break;
    case SelectionHandle::BottomLeft: l += d.x(); b += d.y(); break;
    case SelectionHandle::Left: l += d.x(); break;
    default: return o;
    }
    // 允许拖过对边：规范化
    return QRect(QPoint(std::min(l, r), std::min(t, b)), QPoint(std::max(l, r), std::max(t, b)));
}

QRect moveSelection(const QRect& s, QPoint d, const QRect& bounds)
{
    QRect moved = s.translated(d);
    if (bounds.isValid()) {
        if (moved.left() < bounds.left())
            moved.moveLeft(bounds.left());
        if (moved.top() < bounds.top())
            moved.moveTop(bounds.top());
        if (moved.right() > bounds.right())
            moved.moveRight(bounds.right());
        if (moved.bottom() > bounds.bottom())
            moved.moveBottom(bounds.bottom());
    }
    return moved;
}

QRect nudgeSelection(const QRect& s, Qt::Key key, NudgeKind kind, const QRect& bounds)
{
    QPoint d;
    switch (key) {
    case Qt::Key_Left: d = QPoint(-1, 0); break;
    case Qt::Key_Right: d = QPoint(1, 0); break;
    case Qt::Key_Up: d = QPoint(0, -1); break;
    case Qt::Key_Down: d = QPoint(0, 1); break;
    default: return s;
    }
    if (kind == NudgeKind::Move)
        return moveSelection(s, d, bounds);
    // 扩展：向该方向的边外移一像素；收缩：该方向的边内移一像素
    QRect r = s;
    const int sign = kind == NudgeKind::Expand ? 1 : -1;
    if (d.x() < 0)
        r.setLeft(r.left() - sign);
    else if (d.x() > 0)
        r.setRight(r.right() + sign);
    else if (d.y() < 0)
        r.setTop(r.top() - sign);
    else
        r.setBottom(r.bottom() + sign);
    if (r.width() < 1 || r.height() < 1)
        return s;
    if (bounds.isValid())
        r = r.intersected(bounds);
    return r.isEmpty() ? s : r;
}

} // namespace ss
