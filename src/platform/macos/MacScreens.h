#pragma once

#include "core/ports/ScreenTypes.h"

#include <QList>
#include <cstdint>

namespace ss::mac {

// 枚举显示器。macOS 的屏幕坐标以主屏左下角为原点、向上为正，这里统一转换为
// 以主屏左上角为原点、向下为正的物理像素坐标（与 Windows 一致，NFR-07）。
QList<ScreenInfo> enumerateScreens();

// ScreenInfo.id 中保存的 CGDirectDisplayID
uint32_t displayIdOf(const ScreenInfo& screen);

} // namespace ss::mac
