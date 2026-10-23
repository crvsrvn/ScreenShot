#pragma once

#include "core/ports/IElementDetector.h"

#include <QList>
#include <QThread>

namespace ss::testing {

// 可配置元素矩形；可模拟目标程序挂起（延迟超过超时）。
class FakeElementDetector : public IElementDetector {
public:
    void setElements(QList<QRect> elements) { m_elements = std::move(elements); }
    void setHangMs(int ms) { m_hangMs = ms; }

    std::optional<QRect> elementAt(QPoint physicalPos, std::chrono::milliseconds timeout) override
    {
        if (m_hangMs > 0) {
            QThread::msleep(static_cast<unsigned long>(std::min<long long>(m_hangMs, timeout.count())));
            if (m_hangMs > timeout.count())
                return std::nullopt;
        }
        for (const auto& r : m_elements) {
            if (r.contains(physicalPos))
                return r;
        }
        return std::nullopt;
    }

private:
    QList<QRect> m_elements;
    int m_hangMs = 0;
};

} // namespace ss::testing
