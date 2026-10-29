#include "platform/windows/WinCursor.h"

#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace ss::win {

namespace {

// 当前指针：句柄、位图大小、热点和在屏幕上的放大倍数
struct CursorInfo {
    HCURSOR handle = nullptr;
    QPoint pos;
    QPoint hotspot;
    QSize bitmapSize;
    qreal scale = 1.0;
};

// 系统按“指针大小”设置（CursorBaseSize，如辅助功能里调大的指针）放大绘制指针，位图本身仍是原大小
qreal cursorScale(int bitmapWidth)
{
    DWORD base = 0;
    DWORD size = sizeof(base);
    if (bitmapWidth <= 0 || RegGetValueW(HKEY_CURRENT_USER, L"Control Panel\\Cursors", L"CursorBaseSize", RRF_RT_REG_DWORD, nullptr, &base, &size) != ERROR_SUCCESS)
        return 1.0;
    return std::max(1.0, static_cast<qreal>(base) / bitmapWidth);
}

std::optional<CursorInfo> currentCursor(ICONINFO& ii)
{
    CURSORINFO ci{};
    ci.cbSize = sizeof(ci);
    if (!GetCursorInfo(&ci) || !(ci.flags & CURSOR_SHOWING) || !ci.hCursor)
        return std::nullopt;
    if (!GetIconInfo(ci.hCursor, &ii))
        return std::nullopt;
    BITMAP bm{};
    GetObject(ii.hbmMask, sizeof(bm), &bm);
    CursorInfo c;
    c.handle = ci.hCursor;
    c.pos = QPoint(ci.ptScreenPos.x, ci.ptScreenPos.y);
    c.hotspot = QPoint(static_cast<int>(ii.xHotspot), static_cast<int>(ii.yHotspot));
    c.bitmapSize = QSize(bm.bmWidth, ii.hbmColor ? bm.bmHeight : bm.bmHeight / 2); // 单色指针的掩码图高度是两倍
    c.scale = cursorScale(bm.bmWidth);
    return c;
}

void releaseIconInfo(ICONINFO& ii)
{
    if (ii.hbmColor)
        DeleteObject(ii.hbmColor);
    if (ii.hbmMask)
        DeleteObject(ii.hbmMask);
}

// 指针在屏幕上的物理区域（放大后）
QRect screenRect(const CursorInfo& c)
{
    const QPoint topLeft = c.pos - QPoint(qRound(c.hotspot.x() * c.scale), qRound(c.hotspot.y() * c.scale));
    return QRect(topLeft, QSize(qRound(c.bitmapSize.width() * c.scale), qRound(c.bitmapSize.height() * c.scale)));
}

} // namespace

std::optional<CursorImage> captureCursorImage()
{
    ICONINFO ii{};
    const auto cursor = currentCursor(ii);
    if (!cursor)
        return std::nullopt;
    const QRect rect = screenRect(*cursor);
    const int w = rect.width();
    const int h = rect.height();

    // 画到 32 位 DIB：先画到透明背景得到带 alpha 的图（DrawIconEx 支持 alpha 指针），按放大后的大小画
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
        DrawIconEx(memDc, 0, 0, cursor->handle, w, h, 0, nullptr, DI_NORMAL);
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
                    // AND 掩码：1 = 透明；掩码是原大小，按比例取样
                    const COLORREF m = GetPixel(maskDc, x * cursor->bitmapSize.width() / w, y * cursor->bitmapSize.height() / h);
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
    releaseIconInfo(ii);
    if (result.isNull())
        return std::nullopt;
    CursorImage c;
    c.image = result.convertToFormat(QImage::Format_ARGB32);
    c.physicalTopLeft = rect.topLeft();
    return c;
}

std::optional<QRect> cursorRect()
{
    ICONINFO ii{};
    const auto cursor = currentCursor(ii);
    if (!cursor)
        return std::nullopt;
    releaseIconInfo(ii);
    return screenRect(*cursor);
}

} // namespace ss::win
