#pragma once

#include <QList>
#include <QPoint>
#include <QRect>

namespace ss {

// 选区上的操作位置（CAP-10：8 个锚点、内部移动、外部重新框选）
enum class SelectionHandle { None, TopLeft, Top, TopRight, Right, BottomRight, Bottom, BottomLeft, Left, Inside, Outside };

// 命中测试：physical 为物理像素点，grip 为锚点的半径（物理像素）
SelectionHandle hitTestSelection(const QRect& selection, QPoint physical, int grip);

// 按锚点拖动：返回新的选区（已规范化，至少 1×1）。anchorStart 是拖动开始时的选区。
QRect resizeSelection(const QRect& original, SelectionHandle handle, QPoint delta);

// 整体移动，限制在 bounds 内（bounds 为空时不限制）
QRect moveSelection(const QRect& selection, QPoint delta, const QRect& bounds);

// 键盘微调（CAP-24）：expand=向外扩 1 像素，shrink=向内缩 1 像素，move=移动 1 像素
enum class NudgeKind { Move, Expand, Shrink };
QRect nudgeSelection(const QRect& selection, Qt::Key arrowKey, NudgeKind kind, const QRect& bounds);

// 锚点中心位置（8 个），顺序同 SelectionHandle 的 TopLeft..Left
QList<QPoint> selectionHandlePoints(const QRect& selection);

} // namespace ss
