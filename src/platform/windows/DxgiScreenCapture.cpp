#include "platform/windows/DxgiScreenCapture.h"

#include "core/fx/HdrToneMap.h"
#include "core/util/Clock.h"
#include "platform/windows/WinScreens.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cstring>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace ss::win {

struct DxgiScreenCapture::Output {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGIOutputDuplication> duplication;
    ComPtr<ID3D11Texture2D> staging;
    QImage lastFrame; // 最近一次成功取到的完整画面
    QSize size;
    qreal scale = 1.0;
    bool everAcquired = false;
    bool hdr = false;              // 输出处于 HDR 模式：桌面复制交出 scRGB FP16 帧
    float sdrWhiteNits = 80.0f;    // SDR 白电平
};

namespace {

// SDR 白电平（尼特）：DISPLAYCONFIG_SDR_WHITE_LEVEL，默认 80
float screenSdrWhiteNits(const QString& deviceName)
{
    UINT32 pathCount = 0, modeCount = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS)
        return 80.0f;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr) != ERROR_SUCCESS)
        return 80.0f;
    for (UINT32 i = 0; i < pathCount; ++i) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
        source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        source.header.size = sizeof source;
        source.header.adapterId = paths[i].sourceInfo.adapterId;
        source.header.id = paths[i].sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS || QString::fromWCharArray(source.viewGdiDeviceName) != deviceName)
            continue;
        DISPLAYCONFIG_SDR_WHITE_LEVEL white{};
        white.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL;
        white.header.size = sizeof white;
        white.header.adapterId = paths[i].targetInfo.adapterId;
        white.header.id = paths[i].targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&white.header) == ERROR_SUCCESS)
            return white.SDRWhiteLevel * 80.0f / 1000.0f;
    }
    return 80.0f;
}

} // namespace

DxgiScreenCapture::DxgiScreenCapture()
{
    m_idleRelease.setSingleShot(true);
    m_idleRelease.setInterval(3000);
    QObject::connect(&m_idleRelease, &QTimer::timeout, [this] { reset(); });
}
DxgiScreenCapture::~DxgiScreenCapture() = default;

QList<ScreenInfo> DxgiScreenCapture::screens()
{
    return enumerateScreens();
}

void DxgiScreenCapture::reset()
{
    m_outputs.clear();
}

DxgiScreenCapture::Output* DxgiScreenCapture::outputFor(const ScreenInfo& screen)
{
    if (auto it = m_outputs.find(screen.id); it != m_outputs.end())
        return it->get();

    // 枚举所有显卡的所有输出，按设备名匹配；在输出所属的显卡上创建设备（双显卡笔记本，R-12）
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
        return nullptr;
    for (UINT a = 0;; ++a) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(a, &adapter) == DXGI_ERROR_NOT_FOUND)
            break;
        for (UINT o = 0;; ++o) {
            ComPtr<IDXGIOutput> output;
            if (adapter->EnumOutputs(o, &output) == DXGI_ERROR_NOT_FOUND)
                break;
            DXGI_OUTPUT_DESC desc{};
            output->GetDesc(&desc);
            if (QString::fromWCharArray(desc.DeviceName) != screen.id)
                continue;
            ComPtr<IDXGIOutput1> output1;
            if (FAILED(output.As(&output1)))
                return nullptr;
            auto out = std::make_shared<Output>();
            D3D_FEATURE_LEVEL level;
            if (FAILED(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0,
                                         D3D11_SDK_VERSION, &out->device, &level, &out->context)))
                return nullptr;
            // HDR（CAP-34）：输出为 HDR 色彩空间时按 FP16 复制，取回后做色调映射
            ComPtr<IDXGIOutput6> output6;
            if (SUCCEEDED(output.As(&output6))) {
                DXGI_OUTPUT_DESC1 desc1{};
                if (SUCCEEDED(output6->GetDesc1(&desc1)) && desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020) {
                    out->hdr = true;
                    out->sdrWhiteNits = screenSdrWhiteNits(screen.id);
                }
            }
            bool duplicated = false;
            if (out->hdr) {
                ComPtr<IDXGIOutput5> output5;
                const DXGI_FORMAT formats[] = {DXGI_FORMAT_R16G16B16A16_FLOAT};
                duplicated = SUCCEEDED(output.As(&output5)) && SUCCEEDED(output5->DuplicateOutput1(out->device.Get(), 0, 1, formats, &out->duplication));
                if (!duplicated)
                    out->hdr = false;
            }
            if (!duplicated && FAILED(output1->DuplicateOutput(out->device.Get(), &out->duplication)))
                return nullptr;
            DXGI_OUTDUPL_DESC dd{};
            out->duplication->GetDesc(&dd);
            if (out->hdr && dd.ModeDesc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT)
                out->hdr = false;
            out->size = QSize(static_cast<int>(dd.ModeDesc.Width), static_cast<int>(dd.ModeDesc.Height));
            out->scale = screen.scale;
            D3D11_TEXTURE2D_DESC td{};
            td.Width = dd.ModeDesc.Width;
            td.Height = dd.ModeDesc.Height;
            td.MipLevels = 1;
            td.ArraySize = 1;
            td.Format = out->hdr ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_B8G8R8A8_UNORM;
            td.SampleDesc.Count = 1;
            td.Usage = D3D11_USAGE_STAGING;
            td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            if (FAILED(out->device->CreateTexture2D(&td, nullptr, &out->staging)))
                return nullptr;
            m_outputs.insert(screen.id, out);
            return out.get();
        }
    }
    return nullptr;
}

bool DxgiScreenCapture::acquire(Output& out, QImage& image)
{
    // 第一次取帧要等桌面复制交出完整画面；之后不等待，取不到新帧就用上一帧
    const UINT timeoutMs = out.everAcquired ? 0 : 500;
    ComPtr<IDXGIResource> resource;
    DXGI_OUTDUPL_FRAME_INFO info{};
    HRESULT hr = out.duplication->AcquireNextFrame(timeoutMs, &info, &resource);
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        if (out.lastFrame.isNull())
            return false;
        image = out.lastFrame;
        return true;
    }
    if (FAILED(hr))
        return false; // 包括 ACCESS_LOST，由调用方重建

    // 桌面复制的帧始终是整幅画面（不只是脏区）；累计所有帧直到队列排空，拿最新的一帧
    bool copied = false;
    for (;;) {
        ComPtr<ID3D11Texture2D> tex;
        if (SUCCEEDED(resource.As(&tex))) {
            out.context->CopyResource(out.staging.Get(), tex.Get());
            copied = true;
        }
        out.duplication->ReleaseFrame();
        resource.Reset();
        hr = out.duplication->AcquireNextFrame(0, &info, &resource);
        if (hr != S_OK)
            break;
    }
    if (!copied)
        return false;

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(out.context->Map(out.staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        return false;
    QImage frame;
    if (out.hdr) {
        frame = fx::toneMapScRgb(static_cast<const uint16_t*>(mapped.pData), out.size.width(), out.size.height(), static_cast<int>(mapped.RowPitch), out.sdrWhiteNits, m_hdrStrategy);
    } else {
        frame = QImage(out.size, QImage::Format_RGB32);
        const size_t rowBytes = static_cast<size_t>(out.size.width()) * 4;
        for (int y = 0; y < out.size.height(); ++y)
            std::memcpy(frame.scanLine(y), static_cast<const char*>(mapped.pData) + y * mapped.RowPitch, rowBytes);
    }
    frame.setDevicePixelRatio(out.scale);
    out.context->Unmap(out.staging.Get(), 0);
    out.lastFrame = frame;
    out.everAcquired = true;
    image = frame;
    return true;
}

std::optional<ScreenSnapshot> DxgiScreenCapture::capture(const ScreenInfo& screen)
{
    Output* out = outputFor(screen);
    if (!out)
        return std::nullopt;
    QImage image;
    if (!acquire(*out, image)) {
        // 会话失效：重建一次
        m_outputs.remove(screen.id);
        out = outputFor(screen);
        if (!out || !acquire(*out, image))
            return std::nullopt;
    }
    if (image.size() != screen.physicalGeometry.size()) {
        // 分辨率与显示器布局不一致（刚切换模式），放弃，由上层走 GDI
        m_outputs.remove(screen.id);
        return std::nullopt;
    }
    m_idleRelease.start();
    return ScreenSnapshot{screen, std::move(image), monotonicNs()};
}

} // namespace ss::win
