#pragma once

#include "core/ports/IScreenCapture.h"

#include <QHash>
#include <memory>

namespace ss::win {

// DXGI 桌面复制：每台显示器各自保持一个复制会话（在所属显卡上创建设备），
// 画面无变化时复用上一帧；会话失效（ACCESS_LOST）时重建；仍失败则返回空，由上层改走 GDI。
class DxgiScreenCapture : public IScreenCapture {
public:
    DxgiScreenCapture();
    ~DxgiScreenCapture() override;

    QList<ScreenInfo> screens() override;
    std::optional<ScreenSnapshot> capture(const ScreenInfo& screen) override;
    QString backendName() const override { return QStringLiteral("dxgi"); }

    // 释放全部复制会话（显示器布局变化后调用）
    void reset();

private:
    struct Output;
    Output* outputFor(const ScreenInfo& screen);
    bool acquire(Output& out, QImage& image);

    QHash<QString, std::shared_ptr<Output>> m_outputs;
};

} // namespace ss::win
