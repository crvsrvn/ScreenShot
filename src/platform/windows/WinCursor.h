#pragma once

#include "core/ports/ScreenTypes.h"

#include <optional>

namespace ss::win {

// 当前鼠标指针（含透明通道）及其左上角物理位置（CAP-30）
std::optional<CursorImage> captureCursorImage();

} // namespace ss::win
