#include "platform/windows/WinInputHooks.h"

#include <QDateTime>

#include <windows.h>

namespace ss::win {

namespace {
WinInputHooks* g_instance = nullptr;
}

struct HookDispatch {
    static LRESULT CALLBACK keyboard(int code, WPARAM wParam, LPARAM lParam)
    {
        if (code == HC_ACTION && g_instance) {
            const auto* k = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
            const bool injected = (k->flags & LLKHF_INJECTED) != 0;
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)
                emit g_instance->keyDown(static_cast<int>(k->vkCode), injected, now);
            else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP)
                emit g_instance->keyUp(static_cast<int>(k->vkCode), injected, now);
        }
        return CallNextHookEx(nullptr, code, wParam, lParam);
    }

    static LRESULT CALLBACK mouse(int code, WPARAM wParam, LPARAM lParam)
    {
        if (code == HC_ACTION && g_instance) {
            const auto* m = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);
            const QPoint pos(m->pt.x, m->pt.y);
            int button = 0;
            bool down = false;
            switch (wParam) {
            case WM_LBUTTONDOWN: button = 1; down = true; break;
            case WM_LBUTTONUP: button = 1; break;
            case WM_MBUTTONDOWN: button = 2; down = true; break;
            case WM_MBUTTONUP: button = 2; break;
            case WM_RBUTTONDOWN: button = 3; down = true; break;
            case WM_RBUTTONUP: button = 3; break;
            case WM_MOUSEMOVE: emit g_instance->mouseMoved(pos); break;
            case WM_MOUSEWHEEL: emit g_instance->mouseWheel(static_cast<short>(HIWORD(m->mouseData)), pos); break;
            default: break;
            }
            if (button) {
                emit g_instance->mouseButton(button, down, pos);
                if (g_instance->m_swallowMouse)
                    return 1; // 手势进行中：不传给下面的程序
            }
        }
        return CallNextHookEx(nullptr, code, wParam, lParam);
    }
};

WinInputHooks::WinInputHooks(QObject* parent)
    : QObject(parent)
{
    g_instance = this;
}

WinInputHooks::~WinInputHooks()
{
    uninstall();
    if (g_instance == this)
        g_instance = nullptr;
}

bool WinInputHooks::installKeyboard()
{
    if (!m_keyboard)
        m_keyboard = SetWindowsHookExW(WH_KEYBOARD_LL, &HookDispatch::keyboard, GetModuleHandleW(nullptr), 0);
    return m_keyboard != nullptr;
}

bool WinInputHooks::installMouse()
{
    if (!m_mouse)
        m_mouse = SetWindowsHookExW(WH_MOUSE_LL, &HookDispatch::mouse, GetModuleHandleW(nullptr), 0);
    return m_mouse != nullptr;
}

void WinInputHooks::uninstall()
{
    if (m_keyboard) {
        UnhookWindowsHookEx(static_cast<HHOOK>(m_keyboard));
        m_keyboard = nullptr;
    }
    if (m_mouse) {
        UnhookWindowsHookEx(static_cast<HHOOK>(m_mouse));
        m_mouse = nullptr;
    }
}

} // namespace ss::win
