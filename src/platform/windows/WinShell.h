#pragma once

#include <QStringList>

namespace ss::win {

// 资源管理器里当前选中的文件（PIN-12）：遍历打开的资源管理器窗口，取前台那个的选中项；没有返回空
QStringList selectedExplorerFiles();

// 让窗口出现在所有虚拟桌面上（PIN-56）。依赖 Windows 未公开的虚拟桌面接口，失败返回 false（不影响其他功能）
bool setWindowOnAllDesktops(void* hwnd, bool on);

} // namespace ss::win
