#include "platform/windows/DxgiScreenCapture.h"

#include "core/fx/HdrToneMap.h"
#include "core/util/Clock.h"
#include "platform/windows/GdiScreenCapture.h"
#include "platform/windows/WinCursor.h"
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
    QPoint origin; // 显示器左上角的物理坐标
    qreal scale = 1.0;
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

// 系统用软件绘制指针时（如辅助功能里调大的指针），桌面复制的画面里已经带着指针；
// GDI 抓屏不含指针，用它重抓指针所在的一小块盖回去
void eraseCursor(QImage& frame, const QPoint& origin)
{
    const auto cursor = cursorRect();
    if (!cursor)
        return;
    // 放大倍数是按设置推算的，四周多盖一些留余量（GDI 画面本身不含指针，多盖无害）
    const int margin = cursor->width() / 4;
    const QRect area = cursor->adjusted(-margin, -margin, margin, margin).intersected(QRect(origin, frame.size()));
    if (area.isEmpty())
        return;
    const QImage patch = GdiScreenCapture::captureRect(area);
    if (patch.isNull())
        return;
    const QPoint at = area.topLeft() - origin;
    const size_t rowBytes = static_cast<size_t>(area.width()) * 4;
    for (int y = 0; y < area.height(); ++y)
        std::memcpy(frame.scanLine(at.y() + y) + at.x() * 4, patch.constScanLine(y), rowBytes);
}

} // namespace

DxgiScreenCapture::DxgiScreenCapture()
{
    m_warmUp.setSingleShot(true);
    QObject::connect(&m_warmUp, &QTimer::timeout, [this] {
        for (const ScreenInfo& screen : screens())
            outputFor(screen);
    });
    m_warmUp.start(0);
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
            out->origin = screen.physicalGeometry.topLeft();
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

DxgiScreenCapture::Acquired DxgiScreenCapture::acquire(Output& out, QImage& image)
{
    // 不等待：取不到新画面就用上一帧
    auto reuseLast = [&] {
        if (out.lastFrame.isNull())
            return Acquired::NoImageYet;
        image = out.lastFrame;
        return Acquired::Frame;
    };
    ComPtr<IDXGIResource> resource;
    DXGI_OUTDUPL_FRAME_INFO info{};
    HRESULT hr = out.duplication->AcquireNextFrame(0, &info, &resource);
    if (hr == DXGI_ERROR_WAIT_TIMEOUT)
        return reuseLast();
    if (FAILED(hr))
        return Acquired::Lost; // 包括 ACCESS_LOST，由调用方重建

    // 桌面复制的帧始终是整幅画面（不只是脏区）；累计所有帧直到队列排空，拿最新的一帧。
    // LastPresentTime 为 0 的帧没有桌面画面（只有指针更新；新会话的第一帧是全黑的空白画面），不采用
    bool copied = false;
    for (;;) {
        ComPtr<ID3D11Texture2D> tex;
        if (info.LastPresentTime.QuadPart != 0 && SUCCEEDED(resource.As(&tex))) {
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
        return reuseLast();

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(out.context->Map(out.staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        return Acquired::Lost;
    QImage frame;
    if (out.hdr) {
        frame = fx::toneMapScRgb(static_cast<const uint16_t*>(mapped.pData), out.size.width(), out.size.height(), static_cast<int>(mapped.RowPitch), out.sdrWhiteNits, m_hdrStrategy);
    } else {
        frame = QImage(out.size, QImage::Format_RGB32);
        const size_t rowBytes = static_cast<size_t>(out.size.width()) * 4;
        for (int y = 0; y < out.size.height(); ++y)
            std::memcpy(frame.scanLine(y), static_cast<const char*>(mapped.pData) + y * mapped.RowPitch, rowBytes);
    }
    out.context->Unmap(out.staging.Get(), 0);
    eraseCursor(frame, out.origin);
    frame.setDevicePixelRatio(out.scale);
    out.lastFrame = frame;
    image = frame;
    return Acquired::Frame;
}

std::optional<ScreenSnapshot> DxgiScreenCapture::capture(const ScreenInfo& screen)
{
    Output* out = outputFor(screen);
    if (!out)
        return std::nullopt;
    QImage image;
    Acquired result = acquire(*out, image);
    if (result == Acquired::Lost) {
        // 会话失效：重建一次（新会话通常还没有真实画面，本次多半由上层走 GDI）
        m_outputs.remove(screen.id);
        out = outputFor(screen);
        result = out ? acquire(*out, image) : Acquired::Lost;
    }
    if (result != Acquired::Frame)
        return std::nullopt;
    if (image.size() != screen.physicalGeometry.size()) {
        // 分辨率与显示器布局不一致（刚切换模式），放弃，由上层走 GDI
        m_outputs.remove(screen.id);
        return std::nullopt;
    }
    return ScreenSnapshot{screen, std::move(image), monotonicNs()};
}

} // namespace ss::win
