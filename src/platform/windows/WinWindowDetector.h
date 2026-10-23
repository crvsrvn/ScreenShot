#pragma once

#include "core/ports/IWindowDetector.h"

namespace ss::win {

// 顶层窗口检测：Z 序 + DWM 真实边框（不含阴影），过滤隐藏、最小化、被 cloak 的窗口和本进程窗口。
class WinWindowDetector : public IWindowDetector {
public:
    QList<WindowInfo> topLevelWindows() override;
};

} // namespace ss::win
