#include "platform/windows/WasapiCapture.h"

#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmreg.h>

#include <chrono>

namespace ss::win {

namespace {

template <typename T>
void safeRelease(T*& p)
{
    if (p) {
        p->Release();
        p = nullptr;
    }
}

// 设备格式 → 单声道 16 位（取各声道平均）
void convertToMono16(const BYTE* data, UINT32 frames, const WAVEFORMATEX* fmt, bool isFloat, QVector<qint16>& out)
{
    const int channels = fmt->nChannels;
    out.resize(frames);
    for (UINT32 i = 0; i < frames; ++i) {
        double sum = 0;
        for (int c = 0; c < channels; ++c) {
            if (isFloat) {
                sum += reinterpret_cast<const float*>(data)[i * channels + c];
            } else if (fmt->wBitsPerSample == 16) {
                sum += reinterpret_cast<const qint16*>(data)[i * channels + c] / 32768.0;
            } else if (fmt->wBitsPerSample == 32) {
                sum += reinterpret_cast<const qint32*>(data)[i * channels + c] / 2147483648.0;
            } else if (fmt->wBitsPerSample == 24) {
                const BYTE* p = data + (i * channels + c) * 3;
                const qint32 v = (static_cast<qint32>(static_cast<qint8>(p[2])) << 16) | (p[1] << 8) | p[0];
                sum += v / 8388608.0;
            }
        }
        const double v = std::clamp(sum / channels, -1.0, 1.0);
        out[i] = static_cast<qint16>(v * 32767.0);
    }
}

// 线性重采样到 48 kHz
QVector<qint16> resample(const QVector<qint16>& in, int fromRate, int toRate)
{
    if (fromRate == toRate || in.isEmpty())
        return in;
    const qsizetype n = static_cast<qsizetype>(static_cast<qint64>(in.size()) * toRate / fromRate);
    QVector<qint16> out(n);
    for (qsizetype i = 0; i < n; ++i) {
        const double pos = static_cast<double>(i) * fromRate / toRate;
        const qsizetype a = static_cast<qsizetype>(pos);
        const qsizetype b = std::min(a + 1, in.size() - 1);
        const double t = pos - a;
        out[i] = static_cast<qint16>(in[a] * (1 - t) + in[b] * t);
    }
    return out;
}

bool isFloatFormat(const WAVEFORMATEX* fmt)
{
    if (fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
        return true;
    if (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
        return reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(fmt)->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    return false;
}

IMMDevice* defaultDevice(IAudioCapture::Source source)
{
    IMMDeviceEnumerator* enumerator = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator))))
        return nullptr;
    IMMDevice* device = nullptr;
    enumerator->GetDefaultAudioEndpoint(source == IAudioCapture::Source::Microphone ? eCapture : eRender, eConsole, &device);
    safeRelease(enumerator);
    return device;
}

} // namespace

WasapiCapture::~WasapiCapture()
{
    stop();
}

bool WasapiCapture::available(Source source) const
{
    const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IMMDevice* device = defaultDevice(source);
    const bool ok = device != nullptr;
    safeRelease(device);
    if (SUCCEEDED(init))
        CoUninitialize();
    return ok;
}

bool WasapiCapture::start(Source source)
{
    stop();
    m_running = true;
    m_ok = false;
    m_done = false;
    m_thread = std::thread([this, source] {
        run(source);
        m_done = true;
    });
    // 等待线程完成初始化（最多 2 秒），判断设备是否可用
    for (int i = 0; i < 200 && !m_ok && !m_done; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (!m_ok)
        stop();
    return m_ok;
}

void WasapiCapture::stop()
{
    m_running = false;
    if (m_thread.joinable())
        m_thread.join();
}

QVector<qint16> WasapiCapture::take()
{
    QMutexLocker lock(&m_mutex);
    QVector<qint16> out;
    out.swap(m_buffer);
    return out;
}

void WasapiCapture::run(Source source)
{
    const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IMMDevice* device = defaultDevice(source);
    IAudioClient* client = nullptr;
    IAudioCaptureClient* capture = nullptr;
    WAVEFORMATEX* fmt = nullptr;
    bool ok = device && SUCCEEDED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&client)));
    ok = ok && SUCCEEDED(client->GetMixFormat(&fmt));
    const DWORD flags = source == Source::System ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0;
    ok = ok && SUCCEEDED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 2000000, 0, fmt, nullptr));
    ok = ok && SUCCEEDED(client->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(&capture)));
    ok = ok && SUCCEEDED(client->Start());
    m_ok = ok;
    if (ok) {
        const bool isFloat = isFloatFormat(fmt);
        QVector<qint16> mono;
        while (m_running) {
            UINT32 packet = 0;
            while (SUCCEEDED(capture->GetNextPacketSize(&packet)) && packet > 0) {
                BYTE* data = nullptr;
                UINT32 frames = 0;
                DWORD bufferFlags = 0;
                if (FAILED(capture->GetBuffer(&data, &frames, &bufferFlags, nullptr, nullptr)))
                    break;
                if (bufferFlags & AUDCLNT_BUFFERFLAGS_SILENT) {
                    mono.fill(0, frames);
                } else {
                    convertToMono16(data, frames, fmt, isFloat, mono);
                }
                capture->ReleaseBuffer(frames);
                const QVector<qint16> resampled = resample(mono, static_cast<int>(fmt->nSamplesPerSec), 48000);
                QMutexLocker lock(&m_mutex);
                m_buffer.append(resampled);
                if (!SUCCEEDED(capture->GetNextPacketSize(&packet)))
                    break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        client->Stop();
    }
    if (fmt)
        CoTaskMemFree(fmt);
    safeRelease(capture);
    safeRelease(client);
    safeRelease(device);
    if (SUCCEEDED(init))
        CoUninitialize();
}

} // namespace ss::win
