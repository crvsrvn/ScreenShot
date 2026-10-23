#include "platform/windows/WinScreens.h"

#include <windows.h>
#include <shellscalingapi.h>

namespace ss::win {

namespace {

BOOL CALLBACK monitorProc(HMONITOR monitor, HDC, LPRECT, LPARAM param)
{
    auto* list = reinterpret_cast<QList<ScreenInfo>*>(param);
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info))
        return TRUE;
    UINT dpiX = 96, dpiY = 96;
    if (FAILED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY)))
        dpiX = 96;
    ScreenInfo s;
    s.id = QString::fromWCharArray(info.szDevice);
    s.physicalGeometry = QRect(info.rcMonitor.left, info.rcMonitor.top,
                               info.rcMonitor.right - info.rcMonitor.left,
                               info.rcMonitor.bottom - info.rcMonitor.top);
    s.scale = static_cast<qreal>(dpiX) / 96.0;
    s.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
    s.hdr = false; // HDR 检测待 R-10（需要 HDR 设备）
    list->append(s);
    return TRUE;
}

} // namespace

QList<ScreenInfo> enumerateScreens()
{
    QList<ScreenInfo> list;
    EnumDisplayMonitors(nullptr, nullptr, monitorProc, reinterpret_cast<LPARAM>(&list));
    return list;
}

} // namespace ss::win
