#pragma once

#include "core/ports/IWindowDetector.h"

#include <QList>
#include <QPoint>
#include <QRect>
#include <optional>

namespace ss {

// 窗口检测的纯逻辑部分（CAP-11）：在冻结时刻的窗口列表里找光标下最上层的窗口，
// 并把窗口边框裁剪到虚拟桌面内。
class WindowFinder {
public:
    WindowFinder() = default;
    WindowFinder(QList<WindowInfo> windows, QRect desktopBounds);

    // 光标下按 Z 序最上层的窗口（已裁剪到桌面）
    std::optional<WindowInfo> windowAt(QPoint physical) const;
    const QList<WindowInfo>& windows() const { return m_windows; }

private:
    QList<WindowInfo> m_windows;
    QRect m_bounds;
};

} // namespace ss
