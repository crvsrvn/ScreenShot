#pragma once

#include "core/ports/IGlobalHotkey.h"

#include <QHash>

namespace ss::win {

// RegisterHotKey 实现。用一个仅消息窗口接收 WM_HOTKEY，不依赖 Qt 内部的消息分发细节。
class WinGlobalHotkey : public IGlobalHotkey {
    Q_OBJECT
public:
    explicit WinGlobalHotkey(QObject* parent = nullptr);
    ~WinGlobalHotkey() override;

    bool registerHotkey(int id, const QKeySequence& sequence) override;
    void unregisterHotkey(int id) override;

    // QKeySequence → (修饰键, 虚拟键)；失败返回 false
    static bool toNative(const QKeySequence& sequence, unsigned& modifiers, unsigned& vk);

private:
    void onHotkey(int id);
    friend struct HotkeyWindow;

    void* m_hwnd = nullptr;
    QHash<int, QKeySequence> m_registered;
};

} // namespace ss::win
