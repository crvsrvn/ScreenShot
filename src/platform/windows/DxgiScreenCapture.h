#pragma once

#include "core/fx/HdrToneMap.h"
#include "core/ports/IScreenCapture.h"

#include <QHash>
#include <QTimer>
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
    // HDR 色彩校正策略（CAP-34）
    void setHdrStrategy(fx::HdrStrategy s) { m_hdrStrategy = s; }
    fx::HdrStrategy hdrStrategy() const { return m_hdrStrategy; }

private:
    struct Output;
    Output* outputFor(const ScreenInfo& screen);
    bool acquire(Output& out, QImage& image);

    QHash<QString, std::shared_ptr<Output>> m_outputs;
    QTimer m_idleRelease; // 空闲一段时间后释放复制会话：桌面复制接口数量有限，常驻会挡住同机的另一个实例或录屏软件
    fx::HdrStrategy m_hdrStrategy = fx::HdrStrategy::PreserveHighlights;
};

} // namespace ss::win
