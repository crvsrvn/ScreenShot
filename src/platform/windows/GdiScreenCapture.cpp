#include "platform/windows/GdiScreenCapture.h"

#include "core/util/Clock.h"
#include "platform/windows/WinScreens.h"

#include <windows.h>
#include <cstring>

namespace ss::win {

QList<ScreenInfo> GdiScreenCapture::screens()
{
    return enumerateScreens();
}

QImage GdiScreenCapture::captureRect(const QRect& physical)
{
    if (physical.isEmpty())
        return {};
    HDC screenDc = GetDC(nullptr);
    if (!screenDc)
        return {};
    HDC memDc = CreateCompatibleDC(screenDc);

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = physical.width();
    bmi.bmiHeader.biHeight = -physical.height(); // 自上而下
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(screenDc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    QImage result;
    if (dib && bits) {
        HGDIOBJ old = SelectObject(memDc, dib);
        if (BitBlt(memDc, 0, 0, physical.width(), physical.height(), screenDc, physical.x(), physical.y(),
                   SRCCOPY | CAPTUREBLT)) {
            result = QImage(physical.size(), QImage::Format_RGB32);
            const size_t rowBytes = static_cast<size_t>(physical.width()) * 4;
            for (int y = 0; y < physical.height(); ++y)
                std::memcpy(result.scanLine(y), static_cast<const char*>(bits) + y * rowBytes, rowBytes);
        }
        SelectObject(memDc, old);
    }
    if (dib)
        DeleteObject(dib);
    DeleteDC(memDc);
    ReleaseDC(nullptr, screenDc);
    return result;
}

std::optional<ScreenSnapshot> GdiScreenCapture::capture(const ScreenInfo& screen)
{
    QImage img = captureRect(screen.physicalGeometry);
    if (img.isNull())
        return std::nullopt;
    img.setDevicePixelRatio(screen.scale);
    return ScreenSnapshot{screen, std::move(img), monotonicNs()};
}

} // namespace ss::win
