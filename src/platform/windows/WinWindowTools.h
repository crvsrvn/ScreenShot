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

// 切换光标下方顶层窗口的置顶状态（ACT-14）；返回切换后是否置顶，没有窗口返回 false
bool toggleWindowTopmostAt(QPoint physical);
bool isWindowTopmost(void* hwnd);
// 把光标移到物理像素位置（CAP-24 键盘移动光标）
void setCursorPhysicalPos(QPoint physical);

} // namespace ss::win
