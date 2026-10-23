#pragma once

#include "core/ports/IWindowDetector.h"

namespace ss::testing {

class FakeWindowDetector : public IWindowDetector {
public:
    void setWindows(QList<WindowInfo> windows) { m_windows = std::move(windows); }
    QList<WindowInfo> topLevelWindows() override { return m_windows; }
    void setActiveWindow(std::optional<WindowInfo> w) { m_active = std::move(w); }
    std::optional<WindowInfo> activeWindow() override { return m_active; }

private:
    QList<WindowInfo> m_windows;
    std::optional<WindowInfo> m_active;
};

} // namespace ss::testing
