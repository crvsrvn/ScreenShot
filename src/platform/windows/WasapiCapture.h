#pragma once

#include "core/ports/IVideoEncoder.h"

#include <QMutex>
#include <atomic>
#include <thread>

namespace ss::win {

// WASAPI 采集（VID-04）：麦克风（默认录音设备）或系统声音（默认播放设备的环回）。
// 采集线程把设备格式转成 16 位单声道、重采样到 48 kHz，放进缓冲，由界面线程 take() 取走。
class WasapiCapture : public IAudioCapture {
public:
    ~WasapiCapture() override;
    bool start(Source source) override;
    void stop() override;
    QVector<qint16> take() override;
    int sampleRate() const override { return 48000; }
    bool available(Source source) const override;

private:
    void run(Source source);
    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_ok{false};
    QMutex m_mutex;
    QVector<qint16> m_buffer;
};

} // namespace ss::win
