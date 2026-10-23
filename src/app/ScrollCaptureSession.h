#pragma once

#include "core/ports/IInputSimulator.h"
#include "core/ports/IScreenCapture.h"
#include "core/stitch/Stitcher.h"

#include <QImage>
#include <QObject>
#include <QRect>
#include <QTimer>
#include <memory>

namespace ss {

class Settings;
class ScrollFrameWindow;
class ScrollPreviewPanel;

// 滚动截图会话（CAP-60 ~ CAP-76）：锁定区域，按固定节拍抓取区域画面，画面稳定后交给拼接器；
// 自动滚动时在区域边缘模拟滚轮；连续若干帧无位移视为到底。自身的边框与预览窗口不参与捕获（CAP-72）。
class ScrollCaptureSession : public QObject {
    Q_OBJECT
public:
    enum class State { Idle, Running, Finished };
    Q_ENUM(State)
    enum class Direction { Vertical, Horizontal };

    ScrollCaptureSession(IScreenCapture* capture, IInputSimulator* input, Settings& settings, QObject* parent = nullptr);
    ~ScrollCaptureSession() override;

    // 开始。区域横跨缩放比例不同的显示器时返回 false 并给出原因（CAP-75）。showUi=false 用于无显示器测试。
    bool begin(const QRect& physicalRegion, bool showUi = true);
    QString lastError() const { return m_error; }
    State state() const { return m_state; }
    QRect region() const { return m_region; }

    void setAutoScroll(bool on);
    bool autoScroll() const { return m_autoScroll; }
    // 区域控制（CAP-71）：进行中只能沿拼接方向移动（尺寸不变），到底后也可沿方向拖动继续截取；
    // 未开始 / 已结束时可任意调整。返回是否接受
    bool adjustRegion(const QRect& physicalRegion);
    void setDirection(Direction d) { m_direction = d; }
    Direction direction() const { return m_direction; }

    // 一次节拍：抓帧 → 稳定判断 → 拼接 → 自动滚动。定时器自动调用；测试可手动调用。
    void tick();
    // 结束并产出长图；取消丢弃
    QImage finish();
    void cancel();

    // 状态（CAP-64 预览）
    int stitchedHeight() const { return m_stitcher.image().height(); }
    int frameCount() const { return m_stitcher.frameCount(); }
    bool lastMatchOk() const { return m_lastOk; }
    QString lastReason() const { return m_lastReason; }
    bool reachedEnd() const { return m_reachedEnd; }
    QImage thumbnail(int maxWidth, int maxHeight) const;
    QImage currentImage() const; // 当前长图（纵向），横向模式已转置回来
    int lastShift() const { return m_lastShift; }
    int rejectedFrames() const { return m_rejected; }

signals:
    void statusChanged();
    void finished(const QImage& image, const QRect& physicalRegion);
    void cancelled();
    void outputRequested(const QString& kind); // copy / save / quickSave / pin

private:
    QImage grabRegion();
    void scrollOnce();
    void teardownUi();

    IScreenCapture* m_capture;
    IInputSimulator* m_input;
    Settings& m_settings;
    stitch::Stitcher m_stitcher;
    QRect m_region;
    State m_state = State::Idle;
    Direction m_direction = Direction::Vertical;
    bool m_autoScroll = false;
    QTimer m_timer;
    QImage m_lastFrame;
    QVector<quint64> m_lastHashes;
    QVector<quint64> m_stitchedHashes;
    int m_stableCount = 0;
    bool m_lastOk = true;
    QString m_lastReason;
    bool m_reachedEnd = false;
    int m_lastShift = 0;
    int m_rejected = 0;
    int m_ticksSinceScroll = 0;
    QString m_error;
    ScrollFrameWindow* m_frame = nullptr;
    ScrollPreviewPanel* m_panel = nullptr;
};

} // namespace ss
