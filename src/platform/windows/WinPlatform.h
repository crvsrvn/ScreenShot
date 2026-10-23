#pragma once

#include "core/ports/Platform.h"

namespace ss::win {

// 组装 Windows 平台层。捕获用 DXGI 并在失败时自动改走 GDI。
Platform makeWindowsPlatform();

} // namespace ss::win
