#include "platform/windows/DxgiScreenCapture.h"

#include "core/util/Clock.h"
#include "platform/windows/WinScreens.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <cstring>

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
};

DxgiScreenCapture::DxgiScreenCapture() = default;
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
            if (FAILED(output1->DuplicateOutput(out->device.Get(), &out->duplication)))
                return nullptr;
            DXGI_OUTDUPL_DESC dd{};
            out->duplication->GetDesc(&dd);
            out->size = QSize(static_cast<int>(dd.ModeDesc.Width), static_cast<int>(dd.ModeDesc.Height));
            out->scale = screen.scale;
            D3D11_TEXTURE2D_DESC td{};
            td.Width = dd.ModeDesc.Width;
            td.Height = dd.ModeDesc.Height;
            td.MipLevels = 1;
            td.ArraySize = 1;
            td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
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
    QImage frame(out.size, QImage::Format_RGB32);
    frame.setDevicePixelRatio(out.scale);
    const size_t rowBytes = static_cast<size_t>(out.size.width()) * 4;
    for (int y = 0; y < out.size.height(); ++y)
        std::memcpy(frame.scanLine(y), static_cast<const char*>(mapped.pData) + y * mapped.RowPitch, rowBytes);
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
    return ScreenSnapshot{screen, std::move(image), monotonicNs()};
}

} // namespace ss::win
