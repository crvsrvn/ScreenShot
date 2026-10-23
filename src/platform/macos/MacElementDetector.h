#pragma once

#include "core/ports/IElementDetector.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace ss::mac {

// Accessibility 元素检测（AXUIElementCopyElementAtPosition）。需要辅助功能权限。
// 与 Windows 实现相同的线程模型：独立工作线程 + 调用方超时，挂起的请求不排队。
class MacElementDetector : public IElementDetector {
public:
    MacElementDetector();
    ~MacElementDetector() override;

    std::optional<QRect> elementAt(QPoint physicalPos, std::chrono::milliseconds timeout) override;

private:
    void workerLoop();

    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::function<void()> m_pending;
    bool m_stop = false;
    std::atomic<bool> m_busy{false};
};

} // namespace ss::mac
