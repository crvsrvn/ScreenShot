#include "platform/windows/MfCamera.h"

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

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

IMFActivate* firstCamera()
{
    IMFAttributes* attrs = nullptr;
    if (FAILED(MFCreateAttributes(&attrs, 1)))
        return nullptr;
    attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    IMFActivate** devices = nullptr;
    UINT32 count = 0;
    IMFActivate* first = nullptr;
    if (SUCCEEDED(MFEnumDeviceSources(attrs, &devices, &count)) && count > 0) {
        first = devices[0];
        first->AddRef();
        for (UINT32 i = 0; i < count; ++i)
            devices[i]->Release();
        CoTaskMemFree(devices);
    }
    safeRelease(attrs);
    return first;
}

} // namespace

MfCamera::~MfCamera()
{
    stop();
}

bool MfCamera::available() const
{
    const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool started = SUCCEEDED(MFStartup(MF_VERSION));
    IMFActivate* cam = started ? firstCamera() : nullptr;
    const bool ok = cam != nullptr;
    safeRelease(cam);
    if (started)
        MFShutdown();
    if (SUCCEEDED(init))
        CoUninitialize();
    return ok;
}

bool MfCamera::start()
{
    stop();
    m_running = true;
    m_ok = false;
    std::atomic<bool> done{false};
    m_thread = std::thread([this, &done] {
        run();
        done = true;
    });
    for (int i = 0; i < 300 && !m_ok && !done; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (!m_ok)
        stop();
    return m_ok;
}

void MfCamera::stop()
{
    m_running = false;
    if (m_thread.joinable())
        m_thread.join();
}

QImage MfCamera::latestFrame()
{
    QMutexLocker lock(&m_mutex);
    return m_frame;
}

void MfCamera::run()
{
    const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool started = SUCCEEDED(MFStartup(MF_VERSION));
    IMFActivate* cam = started ? firstCamera() : nullptr;
    IMFMediaSource* source = nullptr;
    IMFSourceReader* reader = nullptr;
    IMFAttributes* attrs = nullptr;
    bool ok = cam && SUCCEEDED(cam->ActivateObject(__uuidof(IMFMediaSource), reinterpret_cast<void**>(&source)));
    if (ok) {
        MFCreateAttributes(&attrs, 1);
        if (attrs)
            attrs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
        ok = SUCCEEDED(MFCreateSourceReaderFromMediaSource(source, attrs, &reader));
    }
    UINT32 width = 0, height = 0;
    bool bottomUp = true;
    if (ok) {
        IMFMediaType* type = nullptr;
        MFCreateMediaType(&type);
        type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        ok = SUCCEEDED(reader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, type));
        safeRelease(type);
        if (ok && SUCCEEDED(reader->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &type))) {
            MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &width, &height);
            const INT32 stride = static_cast<INT32>(MFGetAttributeUINT32(type, MF_MT_DEFAULT_STRIDE, 0));
            bottomUp = stride >= 0;
            safeRelease(type);
        }
        ok = ok && width > 0 && height > 0;
    }
    m_ok = ok;
    while (ok && m_running) {
        DWORD flags = 0;
        IMFSample* sample = nullptr;
        if (FAILED(reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, nullptr, &flags, nullptr, &sample)))
            break;
        if (sample) {
            IMFMediaBuffer* buffer = nullptr;
            if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buffer))) {
                BYTE* data = nullptr;
                DWORD length = 0;
                if (SUCCEEDED(buffer->Lock(&data, nullptr, &length)) && length >= width * height * 4) {
                    QImage img(data, static_cast<int>(width), static_cast<int>(height), static_cast<qsizetype>(width * 4), QImage::Format_RGB32);
                    QImage copy = bottomUp ? img.flipped(Qt::Vertical) : img.copy();
                    buffer->Unlock();
                    QMutexLocker lock(&m_mutex);
                    m_frame = copy;
                }
                safeRelease(buffer);
            }
            safeRelease(sample);
        }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
            break;
    }
    safeRelease(attrs);
    safeRelease(reader);
    if (source)
        source->Shutdown();
    safeRelease(source);
    safeRelease(cam);
    if (started)
        MFShutdown();
    if (SUCCEEDED(init))
        CoUninitialize();
}

} // namespace ss::win
