#pragma once

#include "core/ports/IInputSimulator.h"

namespace ss::win {

// SendInput 实现：先把光标移到目标物理位置，再发滚轮（滚轮事件到达光标下方的窗口）
class WinInputSimulator : public IInputSimulator {
public:
    void mouseWheelAt(QPoint physical, int notches, bool horizontal) override;
};

} // namespace ss::win
