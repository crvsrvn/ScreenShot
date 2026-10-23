#include "ScrollTestWindow.h"

#include <QPainter>

#include <windows.h>
#include <windowsx.h>

namespace ss::driver {

namespace {

// 确定性“文档”：每 24 行一条记录，记录内有若干彩色块
struct Block {
    int x, y, w, h;
    COLORREF color;
};

std::vector<Block> documentBlocks(int width, int docHeight)
{
    std::vector<Block> blocks;
    unsigned seed = 12345;
    auto next = [&] {
        seed = seed * 1103515245u + 12345u;
        return (seed >> 8) & 0xFFFF;
    };
    for (int y = 4; y + 16 < docHeight; y += 24) {
        int x = 8 + static_cast<int>(next() % 20);
        const int count = 3 + static_cast<int>(next() % 4);
        for (int i = 0; i < count && x < width - 20; ++i) {
            const int w = 20 + static_cast<int>(next() % 60);
            const COLORREF c = RGB(next() % 200, next() % 200, next() % 200);
            blocks.push_back({x, y, std::min(w, width - x - 4), 16, c});
            x += w + 8 + static_cast<int>(next() % 20);
        }
    }
    return blocks;
}

} // namespace

ScrollTestWindow::ScrollTestWindow(int clientWidth, int clientHeight, int documentHeight)
    : m_width(clientWidth)
    , m_height(clientHeight)
    , m_docHeight(documentHeight)
{
}

ScrollTestWindow::~ScrollTestWindow()
{
    if (m_hwnd)
        DestroyWindow(static_cast<HWND>(m_hwnd));
}

long long __stdcall ScrollTestWindow::wndProc(void* hwndRaw, unsigned msg, unsigned long long wParam, long long lParam)
{
    HWND hwnd = static_cast<HWND>(hwndRaw);
    auto* self = reinterpret_cast<ScrollTestWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_MOUSEWHEEL:
        if (self) {
            const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            self->m_offset = std::clamp(self->m_offset - delta / WHEEL_DELTA * 60, 0, self->m_docHeight - self->m_height);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        if (self)
            self->paint(hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void ScrollTestWindow::paint(void* hdcRaw)
{
    HDC hdc = static_cast<HDC>(hdcRaw);
    RECT full{0, 0, m_width, m_height};
    HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(hdc, &full, white);
    DeleteObject(white);
    for (const Block& b : documentBlocks(m_width, m_docHeight)) {
        const int top = b.y - m_offset;
        if (top + b.h < 0 || top > m_height)
            continue;
        RECT r{b.x, top, b.x + b.w, top + b.h};
        HBRUSH brush = CreateSolidBrush(b.color);
        FillRect(hdc, &r, brush);
        DeleteObject(brush);
    }
}

bool ScrollTestWindow::create(int x, int y)
{
    WNDCLASSW wc{};
    wc.lpfnWndProc = reinterpret_cast<WNDPROC>(&ScrollTestWindow::wndProc);
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"ScreenShotScrollTestWindow";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    RECT rc{0, 0, m_width, m_height};
    AdjustWindowRectEx(&rc, WS_POPUP | WS_BORDER, FALSE, WS_EX_TOPMOST);
    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST, wc.lpszClassName, L"Scroll test", WS_POPUP | WS_BORDER | WS_VISIBLE, x, y, rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr,
                                wc.hInstance, nullptr);
    if (!hwnd)
        return false;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    m_hwnd = hwnd;
    InvalidateRect(hwnd, nullptr, FALSE);
    pump();
    return true;
}

void ScrollTestWindow::pump()
{
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

QRect ScrollTestWindow::clientPhysicalRect() const
{
    if (!m_hwnd)
        return {};
    POINT origin{0, 0};
    ClientToScreen(static_cast<HWND>(m_hwnd), &origin);
    return QRect(origin.x, origin.y, m_width, m_height);
}

QImage ScrollTestWindow::renderDocument() const
{
    QImage img(m_width, m_docHeight, QImage::Format_RGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    for (const Block& b : documentBlocks(m_width, m_docHeight))
        p.fillRect(QRect(b.x, b.y, b.w, b.h), QColor(GetRValue(b.color), GetGValue(b.color), GetBValue(b.color)));
    return img;
}

} // namespace ss::driver
