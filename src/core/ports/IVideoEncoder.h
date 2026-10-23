#pragma once

#include <QImage>
#include <QSize>
#include <QString>
#include <QVector>
#include <functional>
#include <memory>

namespace ss {

// MP4（H.264 + AAC）编码器（VID-03）。平台层实现（Windows：Media Foundation）；测试替身记录帧。
class IVideoEncoder {
public:
    virtual ~IVideoEncoder() = default;
    // 打开输出文件；sampleRate / channels 为 0 表示没有音轨
    virtual bool open(const QString& path, QSize frameSize, int fps, int audioSampleRate = 0, int audioChannels = 0) = 0;
    // 追加一帧（任意 QImage 格式）；timestampMs 为相对开始的毫秒
    virtual bool addFrame(const QImage& frame, qint64 timestampMs) = 0;
    // 追加交错的 16 位 PCM
    virtual bool addAudio(const QVector<qint16>& interleaved, qint64 timestampMs) = 0;
    virtual bool finish() = 0;
    virtual QString lastError() const = 0;
};

// 音频采集（VID-04）：麦克风或系统声音（环回）
class IAudioCapture {
public:
    virtual ~IAudioCapture() = default;
    enum class Source { Microphone, System };
    virtual bool start(Source source) = 0;
    virtual void stop() = 0;
    // 取走自上次以来采集到的 16 位单声道 PCM（采样率 sampleRate()）
    virtual QVector<qint16> take() = 0;
    virtual int sampleRate() const = 0;
    virtual bool available(Source source) const = 0;
};

// 摄像头（VID-07）
class ICameraCapture {
public:
    virtual ~ICameraCapture() = default;
    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual QImage latestFrame() = 0;
    virtual bool available() const = 0;
};

// 编码器 / 音频采集每次录制新建实例，平台层提供工厂；没有实现时为空
using VideoEncoderFactory = std::function<std::unique_ptr<IVideoEncoder>()>;
using AudioCaptureFactory = std::function<std::unique_ptr<IAudioCapture>()>;

} // namespace ss
