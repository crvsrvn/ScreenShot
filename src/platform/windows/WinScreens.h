#pragma once

#include "core/ports/ScreenTypes.h"

#include <QList>

namespace ss::win {

// 枚举显示器：物理像素矩形、缩放比例、主屏标志。
// 要求进程为 Per-Monitor V2 DPI 感知（Qt 6 在 Windows 10 1703+ 默认如此），
// 此时 EnumDisplayMonitors 返回的矩形就是物理像素。
QList<ScreenInfo> enumerateScreens();

} // namespace ss::win
