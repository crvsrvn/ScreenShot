#pragma once

#include "core/ports/IScreenCapture.h"

namespace ss::testing {

// 合成显示器（TST-03、TST-08）：任意数量、任意位置和缩放比例，画面由确定性图案生成，
// 每个像素可由 (显示器序号, x, y) 算出，便于逐像素验证。
class FakeScreenCapture : public IScreenCapture {
public:
    FakeScreenCapture() = default;
    explicit FakeScreenCapture(QList<ScreenInfo> screens);

    void setScreens(QList<ScreenInfo> screens);
    // 替换某台显示器的画面（默认是图案）
    void setImage(int index, QImage image);
    // 捕获失败模拟
    void setFailing(int index, bool failing);

    QList<ScreenInfo> screens() override { return m_screens; }
    std::optional<ScreenSnapshot> capture(const ScreenInfo& screen) override;
    QString backendName() const override { return QStringLiteral("fake"); }
    void setCursor(std::optional<CursorImage> cursor) { m_cursor = std::move(cursor); }
    std::optional<CursorImage> captureCursor() override { return m_cursor; }

    int captureCount() const { return m_captureCount; }

    // 图案：R = x 低 8 位，G = y 低 8 位，B 由显示器序号和高位坐标决定
    static QRgb patternPixel(int screenIndex, int x, int y);
    static QImage patternImage(int screenIndex, QSize size);

private:
    QList<ScreenInfo> m_screens;
    QList<QImage> m_images;
    QList<bool> m_failing;
    int m_captureCount = 0;
    std::optional<CursorImage> m_cursor;
};

} // namespace ss::testing
