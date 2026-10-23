#pragma once

#include "core/ports/ScreenTypes.h"

#include <QList>
#include <optional>

namespace ss {

// 屏幕捕获接口。平台层实现；测试构建可换成合成画面（TST-03）。
class IScreenCapture {
public:
    virtual ~IScreenCapture() = default;

    // 当前显示器布局。
    virtual QList<ScreenInfo> screens() = 0;

    // 抓取一台显示器。失败返回空（例如远程桌面、显示器刚拔掉）。
    virtual std::optional<ScreenSnapshot> capture(const ScreenInfo& screen) = 0;

    // 抓取全部显示器；单台失败不影响其他显示器。
    virtual QList<ScreenSnapshot> captureAll()
    {
        QList<ScreenSnapshot> result;
        for (const auto& s : screens()) {
            if (auto snap = capture(s))
                result.append(std::move(*snap));
        }
        return result;
    }

    // 实现名称，用于日志和测试报告（例如 "dxgi"、"gdi"、"fake"）。
    virtual QString backendName() const = 0;
};

} // namespace ss
