#include "platform/windows/WinCursor.h"

#include <windows.h>
#include <cstring>

namespace ss::win {

std::optional<CursorImage> captureCursorImage()
{
    CURSORINFO ci{};
    ci.cbSize = sizeof(ci);
    if (!GetCursorInfo(&ci) || !(ci.flags & CURSOR_SHOWING) || !ci.hCursor)
        return std::nullopt;
    ICONINFO ii{};
    if (!GetIconInfo(ci.hCursor, &ii))
        return std::nullopt;
    BITMAP bm{};
    GetObject(ii.hbmMask, sizeof(bm), &bm);
    const int w = bm.bmWidth;
    const int h = ii.hbmColor ? bm.bmHeight : bm.bmHeight / 2; // 单色指针的掩码图高度是两倍

    // 画到 32 位 DIB：先画到透明背景得到带 alpha 的图（DrawIconEx 支持 alpha 指针）
    HDC screenDc = GetDC(nullptr);
    HDC memDc = CreateCompatibleDC(screenDc);
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(screenDc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    QImage result;
    if (dib && bits) {
        HGDIOBJ old = SelectObject(memDc, dib);
        std::memset(bits, 0, static_cast<size_t>(w) * h * 4);
        DrawIconEx(memDc, 0, 0, ci.hCursor, w, h, 0, nullptr, DI_NORMAL);
        result = QImage(w, h, QImage::Format_ARGB32_Premultiplied);
        std::memcpy(result.bits(), bits, static_cast<size_t>(w) * h * 4);
        // 没有 alpha 通道的（单色 / 黑白）指针：DrawIconEx 不写 alpha，用掩码推导
        bool anyAlpha = false;
        for (int y = 0; y < h && !anyAlpha; ++y) {
            const auto* line = reinterpret_cast<const quint32*>(result.constScanLine(y));
            for (int x = 0; x < w; ++x)
                if (line[x] & 0xFF000000u) {
                    anyAlpha = true;
                    break;
                }
        }
        if (!anyAlpha) {
            HDC maskDc = CreateCompatibleDC(screenDc);
            HGDIOBJ oldMask = SelectObject(maskDc, ii.hbmMask);
            for (int y = 0; y < h; ++y) {
                auto* line = reinterpret_cast<quint32*>(result.scanLine(y));
                for (int x = 0; x < w; ++x) {
                    const COLORREF m = GetPixel(maskDc, x, y); // AND 掩码：1 = 透明
                    if (m == RGB(255, 255, 255))
                        line[x] = 0;
                    else
                        line[x] |= 0xFF000000u;
                }
            }
            SelectObject(maskDc, oldMask);
            DeleteDC(maskDc);
        }
        SelectObject(memDc, old);
    }
    if (dib)
        DeleteObject(dib);
    DeleteDC(memDc);
    ReleaseDC(nullptr, screenDc);
    if (ii.hbmColor)
        DeleteObject(ii.hbmColor);
    if (ii.hbmMask)
        DeleteObject(ii.hbmMask);
    if (result.isNull())
        return std::nullopt;
    CursorImage c;
    c.image = result.convertToFormat(QImage::Format_ARGB32);
    c.physicalTopLeft = QPoint(ci.ptScreenPos.x - static_cast<int>(ii.xHotspot), ci.ptScreenPos.y - static_cast<int>(ii.yHotspot));
    return c;
}

} // namespace ss::win
