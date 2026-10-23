#include "platform/macos/MacElementDetector.h"

#import <AppKit/AppKit.h>
#include <ApplicationServices/ApplicationServices.h>

#include <future>
#include <memory>

namespace ss::mac {

MacElementDetector::MacElementDetector()
    : m_thread([this] { workerLoop(); })
{
}

MacElementDetector::~MacElementDetector()
{
    {
        std::lock_guard lock(m_mutex);
        m_stop = true;
    }
    m_cv.notify_all();
    if (m_busy.load())
        m_thread.detach();
    else
        m_thread.join();
}

void MacElementDetector::workerLoop()
{
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
}

std::optional<QRect> MacElementDetector::elementAt(QPoint physicalPos, std::chrono::milliseconds timeout)
{
    if (m_busy.load())
        return std::nullopt;
    auto promise = std::make_shared<std::promise<std::optional<QRect>>>();
    auto future = promise->get_future();
    {
        std::lock_guard lock(m_mutex);
        m_pending = [promise, physicalPos] {
            std::optional<QRect> result;
            NSScreen* primary = [NSScreen screens].firstObject;
            const CGFloat scale = primary ? primary.backingScaleFactor : 1.0;
            AXUIElementRef system = AXUIElementCreateSystemWide();
            AXUIElementSetMessagingTimeout(system, 0.3f); // 与 Windows 的 300 ms 对齐
            AXUIElementRef element = nullptr;
            // AX 用左上角原点的点坐标
            if (AXUIElementCopyElementAtPosition(system, static_cast<float>(physicalPos.x() / scale), static_cast<float>(physicalPos.y() / scale), &element) == kAXErrorSuccess && element) {
                AXValueRef posValue = nullptr;
                AXValueRef sizeValue = nullptr;
                CGPoint pos{};
                CGSize size{};
                if (AXUIElementCopyAttributeValue(element, kAXPositionAttribute, (CFTypeRef*)&posValue) == kAXErrorSuccess
                    && AXUIElementCopyAttributeValue(element, kAXSizeAttribute, (CFTypeRef*)&sizeValue) == kAXErrorSuccess
                    && AXValueGetValue(posValue, (AXValueType)kAXValueCGPointType, &pos) && AXValueGetValue(sizeValue, (AXValueType)kAXValueCGSizeType, &size)) {
                    result = QRect(static_cast<int>(pos.x * scale), static_cast<int>(pos.y * scale), static_cast<int>(size.width * scale), static_cast<int>(size.height * scale));
                }
                if (posValue)
                    CFRelease(posValue);
                if (sizeValue)
                    CFRelease(sizeValue);
                CFRelease(element);
            }
            CFRelease(system);
            promise->set_value(result);
        };
    }
    m_cv.notify_one();
    if (future.wait_for(timeout) != std::future_status::ready)
        return std::nullopt;
    return future.get();
}

} // namespace ss::mac
