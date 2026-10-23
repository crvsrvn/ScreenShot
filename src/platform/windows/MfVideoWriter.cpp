#include "platform/windows/MfVideoWriter.h"

#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <codecapi.h>

namespace ss::win {

struct MfVideoWriter::Impl {
    IMFSinkWriter* writer = nullptr;
};

namespace {

template <typename T>
void safeRelease(T*& p)
{
    if (p) {
        p->Release();
        p = nullptr;
    }
}

QString hrText(const char* what, HRESULT hr)
{
    return QStringLiteral("%1 failed: 0x%2").arg(QLatin1String(what)).arg(static_cast<quint32>(hr), 8, 16, QLatin1Char('0'));
}

// 100 纳秒单位
LONGLONG toHns(qint64 ms) { return static_cast<LONGLONG>(ms) * 10000; }

} // namespace

MfVideoWriter::MfVideoWriter()
    : m_impl(new Impl)
{
}

MfVideoWriter::~MfVideoWriter()
{
    release();
    delete m_impl;
}

void MfVideoWriter::release()
{
    safeRelease(m_impl->writer);
    if (m_started) {
        MFShutdown();
        m_started = false;
    }
}

bool MfVideoWriter::open(const QString& path, QSize frameSize, int fps, int audioSampleRate, int audioChannels)
{
    release();
    m_error.clear();
    // H.264 要求偶数尺寸
    m_size = QSize(std::max(2, frameSize.width() & ~1), std::max(2, frameSize.height() & ~1));
    m_fps = std::max(1, fps);
    m_audioRate = audioSampleRate;
    m_audioChannels = audioChannels;
    HRESULT hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
        m_error = hrText("MFStartup", hr);
        return false;
    }
    m_started = true;
    IMFAttributes* attrs = nullptr;
    MFCreateAttributes(&attrs, 2);
    if (attrs) {
        attrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
        attrs->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_MPEG4);
    }
    hr = MFCreateSinkWriterFromURL(reinterpret_cast<LPCWSTR>(path.utf16()), nullptr, attrs, &m_impl->writer);
    safeRelease(attrs);
    if (FAILED(hr)) {
        m_error = hrText("MFCreateSinkWriterFromURL", hr);
        release();
        return false;
    }
    IMFSinkWriter* w = m_impl->writer;
    // ---- 视频：输出 H.264，输入 RGB32（自底向上）----
    IMFMediaType* out = nullptr;
    MFCreateMediaType(&out);
    out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    out->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    const quint32 bitrate = static_cast<quint32>(std::clamp<qint64>(static_cast<qint64>(m_size.width()) * m_size.height() * m_fps / 8, 500000, 40000000));
    out->SetUINT32(MF_MT_AVG_BITRATE, bitrate);
    out->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize(out, MF_MT_FRAME_SIZE, m_size.width(), m_size.height());
    MFSetAttributeRatio(out, MF_MT_FRAME_RATE, m_fps, 1);
    MFSetAttributeRatio(out, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    DWORD stream = 0;
    hr = w->AddStream(out, &stream);
    safeRelease(out);
    if (FAILED(hr)) {
        m_error = hrText("AddStream(video)", hr);
        release();
        return false;
    }
    m_videoStream = static_cast<int>(stream);
    IMFMediaType* in = nullptr;
    MFCreateMediaType(&in);
    in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    in->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    in->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize(in, MF_MT_FRAME_SIZE, m_size.width(), m_size.height());
    MFSetAttributeRatio(in, MF_MT_FRAME_RATE, m_fps, 1);
    MFSetAttributeRatio(in, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    hr = w->SetInputMediaType(stream, in, nullptr);
    safeRelease(in);
    if (FAILED(hr)) {
        m_error = hrText("SetInputMediaType(video)", hr);
        release();
        return false;
    }
    // ---- 音频：输出 AAC，输入 16 位 PCM ----
    if (m_audioRate > 0 && m_audioChannels > 0) {
        IMFMediaType* aout = nullptr;
        MFCreateMediaType(&aout);
        aout->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        aout->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
        aout->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        aout->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, static_cast<UINT32>(m_audioRate));
        aout->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, static_cast<UINT32>(m_audioChannels));
        aout->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 16000); // 128 kbps
        DWORD astream = 0;
        hr = w->AddStream(aout, &astream);
        safeRelease(aout);
        if (SUCCEEDED(hr)) {
            IMFMediaType* ain = nullptr;
            MFCreateMediaType(&ain);
            ain->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            ain->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
            ain->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
            ain->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, static_cast<UINT32>(m_audioRate));
            ain->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, static_cast<UINT32>(m_audioChannels));
            ain->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, static_cast<UINT32>(2 * m_audioChannels));
            ain->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, static_cast<UINT32>(2 * m_audioChannels * m_audioRate));
            ain->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
            hr = w->SetInputMediaType(astream, ain, nullptr);
            safeRelease(ain);
        }
        if (FAILED(hr)) {
            // 没有 AAC 编码器时退化为无声视频
            m_audioStream = -1;
            m_audioRate = 0;
        } else {
            m_audioStream = static_cast<int>(astream);
        }
    }
    hr = w->BeginWriting();
    if (FAILED(hr)) {
        m_error = hrText("BeginWriting", hr);
        release();
        return false;
    }
    return true;
}

bool MfVideoWriter::addFrame(const QImage& frameIn, qint64 timestampMs)
{
    if (!m_impl->writer)
        return false;
    QImage frame = frameIn.convertToFormat(QImage::Format_RGB32);
    if (frame.size() != m_size)
        frame = frame.scaled(m_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    frame = frame.flipped(Qt::Vertical); // RGB32 默认自底向上
    const DWORD stride = static_cast<DWORD>(m_size.width() * 4);
    const DWORD bytes = stride * static_cast<DWORD>(m_size.height());
    IMFMediaBuffer* buffer = nullptr;
    HRESULT hr = MFCreateMemoryBuffer(bytes, &buffer);
    if (FAILED(hr))
        return false;
    BYTE* data = nullptr;
    buffer->Lock(&data, nullptr, nullptr);
    for (int y = 0; y < m_size.height(); ++y)
        memcpy(data + y * stride, frame.constScanLine(y), stride);
    buffer->Unlock();
    buffer->SetCurrentLength(bytes);
    IMFSample* sample = nullptr;
    MFCreateSample(&sample);
    sample->AddBuffer(buffer);
    sample->SetSampleTime(toHns(timestampMs));
    sample->SetSampleDuration(10000000LL / m_fps);
    hr = m_impl->writer->WriteSample(static_cast<DWORD>(m_videoStream), sample);
    safeRelease(sample);
    safeRelease(buffer);
    if (FAILED(hr))
        m_error = hrText("WriteSample(video)", hr);
    return SUCCEEDED(hr);
}

bool MfVideoWriter::addAudio(const QVector<qint16>& interleaved, qint64 timestampMs)
{
    if (!m_impl->writer || m_audioStream < 0 || interleaved.isEmpty())
        return false;
    const DWORD bytes = static_cast<DWORD>(interleaved.size() * sizeof(qint16));
    IMFMediaBuffer* buffer = nullptr;
    if (FAILED(MFCreateMemoryBuffer(bytes, &buffer)))
        return false;
    BYTE* data = nullptr;
    buffer->Lock(&data, nullptr, nullptr);
    memcpy(data, interleaved.constData(), bytes);
    buffer->Unlock();
    buffer->SetCurrentLength(bytes);
    IMFSample* sample = nullptr;
    MFCreateSample(&sample);
    sample->AddBuffer(buffer);
    sample->SetSampleTime(toHns(timestampMs));
    sample->SetSampleDuration(static_cast<LONGLONG>(interleaved.size()) * 10000000LL / (static_cast<LONGLONG>(m_audioRate) * m_audioChannels));
    const HRESULT hr = m_impl->writer->WriteSample(static_cast<DWORD>(m_audioStream), sample);
    safeRelease(sample);
    safeRelease(buffer);
    return SUCCEEDED(hr);
}

bool MfVideoWriter::finish()
{
    if (!m_impl->writer)
        return false;
    const HRESULT hr = m_impl->writer->Finalize();
    if (FAILED(hr))
        m_error = hrText("Finalize", hr);
    release();
    return SUCCEEDED(hr);
}

} // namespace ss::win
