#pragma once

#include "core/ports/ScreenTypes.h"

#include <optional>

namespace ss::win {

// 当前鼠标指针（含透明通道）及其左上角物理位置（CAP-30）
std::optional<CursorImage> captureCursorImage();
// 当前鼠标指针在屏幕上占的物理区域（已按系统“指针大小”放大）；指针隐藏时为空
std::optional<QRect> cursorRect();

} // namespace ss::win
