#pragma once

#include "core/ports/IVideoEncoder.h"

#include <QMutex>
#include <atomic>
#include <thread>

namespace ss::win {

// 摄像头（VID-07）：Media Foundation 源读取器，取第一台视频采集设备的 RGB32 画面
class MfCamera : public ICameraCapture {
public:
    ~MfCamera() override;
    bool start() override;
    void stop() override;
    QImage latestFrame() override;
    bool available() const override;

private:
    void run();
    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_ok{false};
    std::atomic<bool> m_done{false};
    QMutex m_mutex;
    QImage m_frame;
};

} // namespace ss::win
