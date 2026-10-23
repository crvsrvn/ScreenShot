#pragma once

#include <QImage>
#include <QRect>

namespace ss::driver {

// 端到端用的可滚动窗口：一个 Win32 窗口，画一份确定性的长“文档”，响应滚轮消息滚动。
// 文档同时能渲染成 QImage（用 FillRect 的纯色块，GDI 与 QImage 像素一致），供比对。
class ScrollTestWindow {
public:
    ScrollTestWindow(int clientWidth, int clientHeight, int documentHeight);
    ~ScrollTestWindow();

    bool create(int x, int y);           // 在物理坐标创建并显示（在创建线程上泵消息）
    void pump();                         // 处理待处理消息
    QRect clientPhysicalRect() const;    // 客户区物理矩形
    int offset() const { return m_offset; }
    QImage renderDocument() const;       // 整份文档
    int documentHeight() const { return m_docHeight; }

private:
    static long long __stdcall wndProc(void* hwnd, unsigned msg, unsigned long long wParam, long long lParam);
    void paint(void* hdc);

    int m_width;
    int m_height;
    int m_docHeight;
    int m_offset = 0;
    void* m_hwnd = nullptr;
};

} // namespace ss::driver
