#include "platform/windows/WinGlobalHotkey.h"

#include "core/util/Clock.h"

#include <windows.h>

namespace ss::win {

namespace {

const wchar_t* kClassName = L"ScreenShotHotkeyWindow";

LRESULT CALLBACK hotkeyWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_HOTKEY) {
        auto* self = reinterpret_cast<WinGlobalHotkey*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (self) {
            // 记录时刻后再派发，计时从消息到达开始
            const qint64 now = monotonicNs();
            emit self->triggered(static_cast<int>(wParam), now);
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

WinGlobalHotkey::WinGlobalHotkey(QObject* parent)
    : IGlobalHotkey(parent)
{
    WNDCLASSW wc{};
    wc.lpfnWndProc = hotkeyWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClassName;
    RegisterClassW(&wc); // 重复注册会失败，无妨
    HWND hwnd = CreateWindowExW(0, kClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    if (hwnd)
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    m_hwnd = hwnd;
}

WinGlobalHotkey::~WinGlobalHotkey()
{
    for (auto it = m_registered.cbegin(); it != m_registered.cend(); ++it)
        UnregisterHotKey(static_cast<HWND>(m_hwnd), it.key());
    if (m_hwnd)
        DestroyWindow(static_cast<HWND>(m_hwnd));
}

bool WinGlobalHotkey::toNative(const QKeySequence& sequence, unsigned& modifiers, unsigned& vk)
{
    if (sequence.isEmpty())
        return false;
    const QKeyCombination combo = sequence[0];
    modifiers = MOD_NOREPEAT;
    const Qt::KeyboardModifiers mods = combo.keyboardModifiers();
    if (mods & Qt::ControlModifier)
        modifiers |= MOD_CONTROL;
    if (mods & Qt::ShiftModifier)
        modifiers |= MOD_SHIFT;
    if (mods & Qt::AltModifier)
        modifiers |= MOD_ALT;
    if (mods & Qt::MetaModifier)
        modifiers |= MOD_WIN;

    const int key = combo.key();
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24) {
        vk = VK_F1 + static_cast<unsigned>(key - Qt::Key_F1);
        return true;
    }
    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        vk = static_cast<unsigned>('A' + (key - Qt::Key_A));
        return true;
    }
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        vk = static_cast<unsigned>('0' + (key - Qt::Key_0));
        return true;
    }
    switch (key) {
    case Qt::Key_Print: vk = VK_SNAPSHOT; return true;
    case Qt::Key_Escape: vk = VK_ESCAPE; return true;
    case Qt::Key_Space: vk = VK_SPACE; return true;
    case Qt::Key_Insert: vk = VK_INSERT; return true;
    case Qt::Key_Delete: vk = VK_DELETE; return true;
    case Qt::Key_Home: vk = VK_HOME; return true;
    case Qt::Key_End: vk = VK_END; return true;
    case Qt::Key_PageUp: vk = VK_PRIOR; return true;
    case Qt::Key_PageDown: vk = VK_NEXT; return true;
    default: break;
    }
    // 其余字符键：按当前键盘布局把字符映射为虚拟键（ACT-02 非英文布局）
    if (key > 0 && key < 0x10000) {
        const SHORT r = VkKeyScanW(static_cast<wchar_t>(key));
        if (r != -1) {
            vk = static_cast<unsigned>(r & 0xFF);
            return true;
        }
    }
    return false;
}

bool WinGlobalHotkey::registerHotkey(int id, const QKeySequence& sequence)
{
    unsigned mods = 0, vk = 0;
    if (!m_hwnd || !toNative(sequence, mods, vk))
        return false;
    unregisterHotkey(id);
    if (!RegisterHotKey(static_cast<HWND>(m_hwnd), id, mods, vk))
        return false;
    m_registered.insert(id, sequence);
    return true;
}

void WinGlobalHotkey::unregisterHotkey(int id)
{
    if (m_registered.remove(id))
        UnregisterHotKey(static_cast<HWND>(m_hwnd), id);
}

} // namespace ss::win
