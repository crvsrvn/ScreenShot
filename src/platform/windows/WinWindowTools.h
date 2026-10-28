#pragma once

#include <QRect>

namespace ss::win {

// 用系统接口直接设置窗口的物理像素位置和尺寸（R-03 的精确定位）
void setWindowPhysicalRect(void* hwnd, const QRect& physical);

// 读取窗口当前的物理像素矩形
QRect windowPhysicalRect(void* hwnd);

// 设为“不参与屏幕捕获”（CAP-72；Windows 10 2004+）。失败返回 false。
bool excludeFromCapture(void* hwnd, bool exclude);

// 当前光标的物理像素位置
QPoint cursorPhysicalPos();

// 把光标移到物理像素位置（CAP-24 键盘移动光标）
void setCursorPhysicalPos(QPoint physical);

} // namespace ss::win
