#include "platform/windows/WinElementDetector.h"

// WIN32_LEAN_AND_MEAN 下 uiautomation.h 需要先有完整的 OLE 声明
#include <windows.h>
#include <ole2.h>
#include <oleacc.h>
#include <uiautomation.h>
#include <wrl/client.h>
#include <future>
#include <memory>

using Microsoft::WRL::ComPtr;

namespace ss::win {

WinElementDetector::WinElementDetector()
    : m_thread([this] { workerLoop(); })
{
}

WinElementDetector::~WinElementDetector()
{
    {
        std::lock_guard lock(m_mutex);
        m_stop = true;
    }
    m_cv.notify_all();
    // 若工作线程卡在挂起的目标程序上，不能无限等待：分离它，进程退出时由系统回收
    if (m_busy.load())
        m_thread.detach();
    else
        m_thread.join();
}

void WinElementDetector::workerLoop()
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock lock(m_mutex);
            m_cv.wait(lock, [&] { return m_stop || m_pending; });
            if (m_stop)
                break;
            task = std::move(m_pending);
            m_pending = nullptr;
        }
        m_busy = true;
        task();
        m_busy = false;
    }
    CoUninitialize();
}

std::optional<QRect> WinElementDetector::elementAt(QPoint physicalPos, std::chrono::milliseconds timeout)
{
    if (m_busy.load())
        return std::nullopt; // 上一个请求还卡着，直接拒绝，不排队

    auto promise = std::make_shared<std::promise<std::optional<QRect>>>();
    auto future = promise->get_future();
    {
        std::lock_guard lock(m_mutex);
        m_pending = [promise, physicalPos] {
            std::optional<QRect> result;
            ComPtr<IUIAutomation> automation;
            // 每个任务重新取一次接口（同一 MTA 内 CoCreateInstance 很便宜，也避免跨线程传递 COM 指针）
            if (SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation8, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation)))) {
                // 默认连接超时 2 s、事务超时 20 s，对交互来说太长（见架构文档 Windows 平台层）
                ComPtr<IUIAutomation2> automation2;
                if (SUCCEEDED(automation.As(&automation2))) {
                    automation2->put_ConnectionTimeout(300);
                    automation2->put_TransactionTimeout(300);
                }
                ComPtr<IUIAutomationElement> element;
                POINT pt{physicalPos.x(), physicalPos.y()};
                if (SUCCEEDED(automation->ElementFromPoint(pt, &element)) && element) {
                    RECT r{};
                    if (SUCCEEDED(element->get_CurrentBoundingRectangle(&r)) && r.right > r.left && r.bottom > r.top)
                        result = QRect(r.left, r.top, r.right - r.left, r.bottom - r.top);
                }
            }
            promise->set_value(result);
        };
    }
    m_cv.notify_one();
    if (future.wait_for(timeout) != std::future_status::ready) {
        ++m_timeouts;
        return std::nullopt;
    }
    return future.get();
}

} // namespace ss::win
