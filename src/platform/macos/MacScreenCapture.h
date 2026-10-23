#pragma once

#include "core/ports/IScreenCapture.h"

namespace ss::mac {

// ScreenCaptureKit 单帧捕获（SCScreenshotManager，macOS 14+）。需要屏幕录制权限。
// 捕获时用内容过滤器排除本程序自己的窗口（CAP-72）。
class MacScreenCapture : public IScreenCapture {
public:
    QList<ScreenInfo> screens() override;
    std::optional<ScreenSnapshot> capture(const ScreenInfo& screen) override;
    QString backendName() const override { return QStringLiteral("screencapturekit"); }
};

} // namespace ss::mac
