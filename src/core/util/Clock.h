#pragma once

#include <QtGlobal>

namespace ss {

// 单调时钟，纳秒。Windows 上基于 QueryPerformanceCounter，跨进程可比，用于端到端计时。
qint64 monotonicNs();

inline double nsToMs(qint64 ns) { return static_cast<double>(ns) / 1e6; }

} // namespace ss
