#pragma once

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
};

} // namespace ss
