#pragma once

#include "core/annotation/AnnotationDocument.h"
#include "core/ports/IScreenCapture.h"
#include "core/ports/IVideoEncoder.h"
#include "core/video/Recording.h"

#include <QObject>
#include <QPointer>
#include <QRect>
#include <QSet>
#include <QTimer>
#include <algorithm>
#include <functional>
#include <memory>

namespace ss {

class Settings;
struct Platform;
class RecordFrameWindow;
class RecordToolbar;
class RecordAnnotateWindow;

// 屏幕录制会话（VID-01 ~ VID-07、VID-09）：锁定区域后按帧率抓取区域画面。
// 普通模式把帧写到磁盘目录（FrameStore，可恢复），停止后交给回放窗口编辑导出；
// 快速模式边录边交给平台编码器写 MP4。音频、摄像头、标注与输入可视化在抓帧时合成。
class RecordSession : public QObject {
    Q_OBJECT
public:
    enum class State { Idle, Countdown, Recording, Paused, Stopped };
    Q_ENUM(State)
    enum class Mode { Normal, Quick };

    RecordSession(IScreenCapture* capture, Platform& platform, Settings& settings, const QString& dataDir, QObject* parent = nullptr);
    ~RecordSession() override;

    // 准备：显示边框与工具栏，不开始录制。showUi=false 用于无显示器测试
    bool begin(const QRect& physicalRegion, bool showUi = true);
    QString lastError() const { return m_error; }

    // 工具栏动作（VID-01）
    void start();   // 按 record.delaySec 倒计时后开始
    void pause();
    void resume();
    void stop();    // 结束：普通模式帧落盘 finished()；快速模式文件写完 finished()
    void restart(); // 重录：丢弃已录内容，回到准备状态
    void cancel();
    // 一次节拍：倒计时 / 抓帧。定时器自动调用；测试可手动调用
    void tick();
    // 测试：替换时钟（毫秒）
    void setClock(std::function<qint64()> clock) { m_clock = std::move(clock); }

    State state() const { return m_state; }
    Mode mode() const { return m_mode; }
    void setMode(Mode mode);
    QRect region() const { return m_region; }
    qint64 elapsedMs() const;
    qint64 countdownRemainingMs() const;
    int frameCount() const { return m_frameCount; }
    int fps() const { return m_fps; }
    void setFps(int fps) { m_fps = std::clamp(fps, 1, 60); }
    FrameStore::Quality quality() const { return m_quality; }
    void setQuality(FrameStore::Quality q) { m_quality = q; }
    int delaySec() const { return m_delaySec; }
    void setDelaySec(int sec) { m_delaySec = std::clamp(sec, 0, 10); }
    // 普通模式的帧目录（停止后由回放窗口接管）
    std::shared_ptr<FrameStore> store() const { return m_store; }
    QString quickFilePath() const { return m_quickPath; }
    const QList<InputEvent>& events() const { return m_events; }

    // 全局输入事件（钩子或测试喂入；物理坐标）（VID-06）
    void noteMouseButton(int button, bool down, QPoint physical);
    void noteWheel(int delta, QPoint physical);
    void noteKey(int virtualKey, bool down);
    static QString keyName(int virtualKey);

    // 录制中标注（VID-05）
    void setAnnotating(bool on);
    bool annotating() const { return m_annotating; }
    AnnotationDocument& annotations() { return m_annotations; }
    // 摄像头画中画（VID-07）
    void setCameraEnabled(bool on);
    bool cameraEnabled() const { return m_cameraEnabled; }
    // 声音（VID-04）
    void setMicEnabled(bool on) { m_micEnabled = on; }
    void setSystemAudioEnabled(bool on) { m_systemEnabled = on; }
    bool micEnabled() const { return m_micEnabled; }
    bool systemAudioEnabled() const { return m_systemEnabled; }
    int audioSampleCount() const { return m_audioSamples; }

signals:
    void stateChanged();
    void ticked();
    void finished();
    void cancelled();

private:
    void beginRecording();
    void captureFrame();
    void captureAudio();
    QImage composite(QImage frame, qint64 timeMs) const;
    bool openOutput();
    void closeOutput(bool keep);
    void teardownUi();
    void setState(State s);
    qint64 nowMs() const { return m_clock ? m_clock() : defaultNowMs(); }
    static qint64 defaultNowMs();

    IScreenCapture* m_capture;
    Platform& m_platform;
    Settings& m_settings;
    QString m_dataDir;
    QRect m_region;
    State m_state = State::Idle;
    Mode m_mode = Mode::Normal;
    int m_fps = 30;
    FrameStore::Quality m_quality = FrameStore::Quality::Standard;
    int m_delaySec = 0;
    QString m_error;
    std::function<qint64()> m_clock;
    QTimer m_timer;
    qint64 m_startAt = 0;      // 录制开始（不含暂停）
    qint64 m_pauseAt = 0;
    qint64 m_pausedTotal = 0;
    qint64 m_countdownEnd = 0;
    qint64 m_lastIndexWrite = 0;
    int m_frameCount = 0;
    std::shared_ptr<FrameStore> m_store;
    std::unique_ptr<IVideoEncoder> m_encoder;
    QString m_quickPath;
    QList<InputEvent> m_events;
    QSet<int> m_modifiersDown;
    // 音频
    bool m_micEnabled = false;
    bool m_systemEnabled = false;
    std::unique_ptr<IAudioCapture> m_mic;
    std::unique_ptr<IAudioCapture> m_system;
    int m_audioSamples = 0; // 已写入的样本数（单声道）
    // 标注 / 摄像头
    bool m_annotating = false;
    AnnotationDocument m_annotations;
    bool m_cameraEnabled = false;
    // 界面
    RecordFrameWindow* m_frame = nullptr;
    RecordToolbar* m_toolbar = nullptr;
    RecordAnnotateWindow* m_annotateWindow = nullptr;
};

} // namespace ss
