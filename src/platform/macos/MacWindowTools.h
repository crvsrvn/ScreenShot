#pragma once

#include <QRect>

namespace ss::mac {

// 把 Qt 创建的窗口设为浮动层级并加入所有空间（PIN-56、R-13）。hwnd 为 QWindow::winId() 得到的 NSView*。
void makeFloatingOnAllSpaces(void* nsView, bool floating);

// 用系统接口按物理像素定位窗口（R-03 的 macOS 对应）
void setWindowPhysicalRect(void* nsView, const QRect& physical);

// 当前光标的物理像素位置（左上角原点）
QPoint cursorPhysicalPos();

} // namespace ss::mac
