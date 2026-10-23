#pragma once

#include <QList>
#include <QRect>
#include <QString>
#include <optional>

namespace ss {

// 一个顶层窗口。frame 不含阴影（CAP-11），物理像素。
struct WindowInfo {
    quintptr handle = 0;
    QString title;
    QString processName;
    QRect frame;
    int zOrder = 0; // 0 为最上层
};

class IWindowDetector {
public:
    virtual ~IWindowDetector() = default;

    // 按 Z 序从上到下返回可见、未被完全遮蔽的顶层窗口，不含本程序自己的窗口。
    virtual QList<WindowInfo> topLevelWindows() = 0;
    // 当前活动窗口（CAP-06）；没有返回空
    virtual std::optional<WindowInfo> activeWindow() { return std::nullopt; }
};

} // namespace ss
