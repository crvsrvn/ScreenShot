#include "platform/windows/WinWindowTools.h"

#include <windows.h>

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

namespace ss::win {

void setWindowPhysicalRect(void* hwnd, const QRect& physical)
{
    SetWindowPos(static_cast<HWND>(hwnd), nullptr, physical.x(), physical.y(), physical.width(), physical.height(),
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

QRect windowPhysicalRect(void* hwnd)
{
    RECT r{};
    if (!GetWindowRect(static_cast<HWND>(hwnd), &r))
        return {};
    return QRect(r.left, r.top, r.right - r.left, r.bottom - r.top);
}

bool excludeFromCapture(void* hwnd, bool exclude)
{
    return SetWindowDisplayAffinity(static_cast<HWND>(hwnd), exclude ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE) != 0;
}

QPoint cursorPhysicalPos()
{
    POINT p{};
    GetCursorPos(&p);
    return QPoint(p.x, p.y);
}

void setCursorPhysicalPos(QPoint physical)
{
    SetCursorPos(physical.x(), physical.y());
}

bool isWindowTopmost(void* hwnd)
{
    return (GetWindowLongPtrW(static_cast<HWND>(hwnd), GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
}

bool toggleWindowTopmostAt(QPoint physical)
{
    HWND hwnd = WindowFromPoint(POINT{physical.x(), physical.y()});
    if (!hwnd)
        return false;
    hwnd = GetAncestor(hwnd, GA_ROOT);
    if (!hwnd)
        return false;
    const bool topmost = isWindowTopmost(hwnd);
    SetWindowPos(hwnd, topmost ? HWND_NOTOPMOST : HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    return !topmost;
}

} // namespace ss::win
