#pragma once

#include "core/ports/IVideoEncoder.h"

namespace ss::win {

// Media Foundation 接收器写出 MP4（H.264 视频 + AAC 音频，VID-03）。系统自带编码器，无需 FFmpeg。
class MfVideoWriter : public IVideoEncoder {
public:
    MfVideoWriter();
    ~MfVideoWriter() override;
    bool open(const QString& path, QSize frameSize, int fps, int audioSampleRate, int audioChannels) override;
    bool addFrame(const QImage& frame, qint64 timestampMs) override;
    bool addAudio(const QVector<qint16>& interleaved, qint64 timestampMs) override;
    bool finish() override;
    QString lastError() const override { return m_error; }

private:
    void release();
    struct Impl;
    Impl* m_impl = nullptr;
    QString m_error;
    QSize m_size;
    int m_fps = 30;
    int m_audioRate = 0;
    int m_audioChannels = 0;
    int m_videoStream = -1;
    int m_audioStream = -1;
    bool m_started = false;
};

} // namespace ss::win
