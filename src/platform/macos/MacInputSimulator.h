#pragma once

#include "core/ports/IInputSimulator.h"

namespace ss::mac {

// CGEvent 实现（需要辅助功能权限；未经真机验证）
class MacInputSimulator : public IInputSimulator {
public:
    void mouseWheelAt(QPoint physical, int notches, bool horizontal) override;
};

} // namespace ss::mac
