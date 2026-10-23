#pragma once

#include "core/ports/IVideoEncoder.h"

#include <QFile>
#include <QtMath>

namespace ss::testing {

// 假编码器：把统计写成文本文件 "fake-mp4 frames=N audio=M"，测试据此验证
class FakeVideoEncoder : public IVideoEncoder {
public:
    bool open(const QString& path, QSize frameSize, int fps, int audioSampleRate, int audioChannels) override
    {
        m_path = path;
        m_size = frameSize;
        m_fps = fps;
        m_audioRate = audioSampleRate;
        m_channels = audioChannels;
        m_frames = 0;
        m_audio = 0;
        return !path.isEmpty();
    }
    bool addFrame(const QImage& frame, qint64) override
    {
        if (frame.isNull())
            return false;
        ++m_frames;
        return true;
    }
    bool addAudio(const QVector<qint16>& samples, qint64) override
    {
        m_audio += samples.size();
        return true;
    }
    bool finish() override
    {
        QFile f(m_path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        f.write(QStringLiteral("fake-mp4 frames=%1 audio=%2 fps=%3 size=%4x%5 rate=%6 channels=%7")
                    .arg(m_frames).arg(m_audio).arg(m_fps).arg(m_size.width()).arg(m_size.height()).arg(m_audioRate).arg(m_channels)
                    .toUtf8());
        return true;
    }
    QString lastError() const override { return {}; }

private:
    QString m_path;
    QSize m_size;
    int m_fps = 0;
    int m_audioRate = 0;
    int m_channels = 0;
    int m_frames = 0;
    qint64 m_audio = 0;
};

// 假音频：每次 take() 给出 take 调用间隔对应的 440 Hz 正弦（固定 480 个样本 = 10 ms）
class FakeAudioCapture : public IAudioCapture {
public:
    bool start(Source source) override
    {
        m_source = source;
        m_running = true;
        return true;
    }
    void stop() override { m_running = false; }
    QVector<qint16> take() override
    {
        QVector<qint16> out;
        if (!m_running)
            return out;
        out.resize(samplesPerTake);
        for (int i = 0; i < samplesPerTake; ++i)
            out[i] = static_cast<qint16>(8000 * qSin(2 * M_PI * 440 * (m_phase + i) / 48000.0));
        m_phase += samplesPerTake;
        return out;
    }
    int sampleRate() const override { return 48000; }
    bool available(Source) const override { return true; }
    static inline int samplesPerTake = 480;

private:
    Source m_source = Source::Microphone;
    bool m_running = false;
    qint64 m_phase = 0;
};

// 假摄像头：160×120 的绿色画面
class FakeCamera : public ICameraCapture {
public:
    bool start() override
    {
        m_running = true;
        return true;
    }
    void stop() override { m_running = false; }
    QImage latestFrame() override
    {
        if (!m_running)
            return {};
        QImage img(160, 120, QImage::Format_RGB32);
        img.fill(QColor(0, 200, 0));
        return img;
    }
    bool available() const override { return true; }
    bool running() const { return m_running; }

private:
    bool m_running = false;
};

} // namespace ss::testing
