#pragma once

#include "core/ports/IElementDetector.h"

#include <QString>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace ss::win {

// UI Automation 元素检测。所有 UIA 调用在独立的 MTA 线程进行（UIA 线程要求），
// 调用方只等待 timeout；超时后立即返回空，挂起的调用在后台继续，期间新请求直接拒绝（NFR-09、R-09）。
class WinElementDetector : public IElementDetector {
public:
    WinElementDetector();
    ~WinElementDetector() override;

    std::optional<QRect> elementAt(QPoint physicalPos, std::chrono::milliseconds timeout) override;

    // 上一次超时的次数（测试用）
    int timeoutCount() const { return m_timeouts.load(); }
    bool busy() const { return m_busy.load(); }

private:
    void workerLoop();

    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::function<void()> m_pending; // 待执行任务
    bool m_stop = false;
    std::atomic<bool> m_busy{false};
    std::atomic<int> m_timeouts{0};
};

} // namespace ss::win
