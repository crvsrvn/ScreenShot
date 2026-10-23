#include "core/capture/WindowFinder.h"

namespace ss {

WindowFinder::WindowFinder(QList<WindowInfo> windows, QRect desktopBounds)
    : m_windows(std::move(windows))
    , m_bounds(desktopBounds)
{
    std::stable_sort(m_windows.begin(), m_windows.end(), [](const WindowInfo& a, const WindowInfo& b) { return a.zOrder < b.zOrder; });
}

std::optional<WindowInfo> WindowFinder::windowAt(QPoint p) const
{
    for (const auto& w : m_windows) {
        const QRect frame = m_bounds.isValid() ? w.frame.intersected(m_bounds) : w.frame;
        if (frame.contains(p)) {
            WindowInfo clipped = w;
            clipped.frame = frame;
            return clipped;
        }
    }
    return std::nullopt;
}

} // namespace ss
