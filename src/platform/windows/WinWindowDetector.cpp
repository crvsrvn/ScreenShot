#include "platform/windows/WinWindowDetector.h"

#include <windows.h>
#include <dwmapi.h>

namespace ss::win {

namespace {

QString processNameOf(HWND hwnd)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h)
        return {};
    wchar_t path[MAX_PATH];
    DWORD len = MAX_PATH;
    QString name;
    if (QueryFullProcessImageNameW(h, 0, path, &len)) {
        name = QString::fromWCharArray(path, static_cast<int>(len));
        name = name.mid(name.lastIndexOf(QLatin1Char('\\')) + 1);
    }
    CloseHandle(h);
    return name;
}

BOOL CALLBACK enumProc(HWND hwnd, LPARAM param)
{
    auto* list = reinterpret_cast<QList<WindowInfo>*>(param);
    if (!IsWindowVisible(hwnd) || IsIconic(hwnd))
        return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == GetCurrentProcessId())
        return TRUE;
    BOOL cloaked = FALSE;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked)
        return TRUE;
    RECT frame{};
    if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame))))
        GetWindowRect(hwnd, &frame);
    const QRect r(frame.left, frame.top, frame.right - frame.left, frame.bottom - frame.top);
    if (r.isEmpty())
        return TRUE;
    const LONG exStyle = GetWindowLongW(hwnd, GWL_EXSTYLE);
    wchar_t title[512];
    const int n = GetWindowTextW(hwnd, title, 512);
    // 无标题的工具窗口多为浮层，跳过
    if ((exStyle & WS_EX_TOOLWINDOW) && n == 0)
        return TRUE;
    WindowInfo w;
    w.handle = reinterpret_cast<quintptr>(hwnd);
    w.title = QString::fromWCharArray(title, n);
    w.processName = processNameOf(hwnd);
    w.frame = r;
    w.zOrder = static_cast<int>(list->size());
    list->append(w);
    return TRUE;
}

} // namespace

QList<WindowInfo> WinWindowDetector::topLevelWindows()
{
    QList<WindowInfo> list;
    EnumWindows(enumProc, reinterpret_cast<LPARAM>(&list));
    return list;
}

std::optional<WindowInfo> WinWindowDetector::activeWindow()
{
    HWND fg = GetForegroundWindow();
    if (!fg)
        return std::nullopt;
    for (const auto& w : topLevelWindows())
        if (w.handle == reinterpret_cast<quintptr>(fg))
            return w;
    return std::nullopt;
}

} // namespace ss::win
