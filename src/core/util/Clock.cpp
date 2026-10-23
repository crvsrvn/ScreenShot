#include "core/util/Clock.h"

#include <chrono>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace ss {

namespace {
constexpr qint64 kNsPerSec = 1000000000LL;
}

qint64 monotonicNs()
{
#ifdef Q_OS_WIN
    static const qint64 freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return static_cast<qint64>(f.QuadPart);
    }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    // 先除再乘会丢精度；拆成秒和余数两部分计算
    const qint64 sec = c.QuadPart / freq;
    const qint64 rem = c.QuadPart % freq;
    return sec * kNsPerSec + rem * kNsPerSec / freq;
#else
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
#endif
}

} // namespace ss
