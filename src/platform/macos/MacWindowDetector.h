#pragma once

#include "core/ports/IWindowDetector.h"

namespace ss::mac {

// 系统窗口列表（CGWindowListCopyWindowInfo）。读取标题需要屏幕录制权限。
class MacWindowDetector : public IWindowDetector {
public:
    QList<WindowInfo> topLevelWindows() override;
};

} // namespace ss::mac
