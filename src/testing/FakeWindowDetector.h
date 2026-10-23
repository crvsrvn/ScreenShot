#pragma once

#include "core/ports/IWindowDetector.h"

namespace ss::testing {

class FakeWindowDetector : public IWindowDetector {
public:
    void setWindows(QList<WindowInfo> windows) { m_windows = std::move(windows); }
    QList<WindowInfo> topLevelWindows() override { return m_windows; }

private:
    QList<WindowInfo> m_windows;
};

} // namespace ss::testing
