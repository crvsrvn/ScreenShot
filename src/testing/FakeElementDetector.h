#pragma once

#include "core/ports/IElementDetector.h"

#include <QList>
#include <QThread>
#include <algorithm>

namespace ss::testing {

// 可配置元素矩形；可模拟目标程序挂起（延迟超过超时）。
class FakeElementDetector : public IElementDetector {
public:
    void setElements(QList<QRect> elements) { m_elements = std::move(elements); }
    void setHangMs(int ms) { m_hangMs = ms; }
    // 层级：包含该点的全部元素，按面积从小到大（最内层在前）
    QList<QRect> elementChainAt(QPoint physicalPos, std::chrono::milliseconds timeout) override
    {
        QList<QRect> chain;
        if (!elementAt(physicalPos, timeout) && m_hangMs > timeout.count())
            return chain;
        for (const auto& r : m_elements)
            if (r.contains(physicalPos))
                chain.append(r);
        std::sort(chain.begin(), chain.end(), [](const QRect& a, const QRect& b) { return a.width() * a.height() < b.width() * b.height(); });
        return chain;
    }

    std::optional<QRect> elementAt(QPoint physicalPos, std::chrono::milliseconds timeout) override
    {
        if (m_hangMs > 0) {
            QThread::msleep(static_cast<unsigned long>(std::min<long long>(m_hangMs, timeout.count())));
            if (m_hangMs > timeout.count())
                return std::nullopt;
        }
        std::optional<QRect> best;
        for (const auto& r : m_elements) {
            if (r.contains(physicalPos) && (!best || r.width() * r.height() < best->width() * best->height()))
                best = r;
        }
        return best;
    }

private:
    QList<QRect> m_elements;
    int m_hangMs = 0;
};

} // namespace ss::testing
