#pragma once

#include <QList>
#include <QPoint>
#include <QRect>
#include <chrono>
#include <optional>

namespace ss {

// UI 元素检测（CAP-12）。实现必须在工作线程完成，并在 timeout 内返回（NFR-09）。
class IElementDetector {
public:
    virtual ~IElementDetector() = default;

    // 返回鼠标下控件的物理像素边界；超时或找不到返回空。
    virtual std::optional<QRect> elementAt(QPoint physicalPos, std::chrono::milliseconds timeout) = 0;
    // 层级（CAP-13）：从最内层元素到其所在窗口的祖先链（不含桌面）。默认只有最内层。
    virtual QList<QRect> elementChainAt(QPoint physicalPos, std::chrono::milliseconds timeout)
    {
        QList<QRect> chain;
        if (auto r = elementAt(physicalPos, timeout))
            chain.append(*r);
        return chain;
    }
};

} // namespace ss
