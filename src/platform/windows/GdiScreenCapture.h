#pragma once

#include "core/ports/IScreenCapture.h"

namespace ss::win {

// GDI 截屏：BitBlt 兜底方案，远程桌面和虚拟机下可用。
class GdiScreenCapture : public IScreenCapture {
public:
    QList<ScreenInfo> screens() override;
    std::optional<ScreenSnapshot> capture(const ScreenInfo& screen) override;
    QString backendName() const override { return QStringLiteral("gdi"); }

    // 直接抓取任意物理矩形（供测试驱动做参照）
    static QImage captureRect(const QRect& physical);
};

} // namespace ss::win
