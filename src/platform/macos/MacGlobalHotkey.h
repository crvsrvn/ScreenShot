#pragma once

#include "core/ports/IGlobalHotkey.h"

#include <QHash>

namespace ss::mac {

// Carbon RegisterEventHotKey：无需额外权限的系统热键接口。
class MacGlobalHotkey : public IGlobalHotkey {
    Q_OBJECT
public:
    explicit MacGlobalHotkey(QObject* parent = nullptr);
    ~MacGlobalHotkey() override;

    bool registerHotkey(int id, const QKeySequence& sequence) override;
    void unregisterHotkey(int id) override;

    // QKeySequence → (Carbon 修饰键, 虚拟键码)
    static bool toNative(const QKeySequence& sequence, unsigned& modifiers, unsigned& keyCode);

private:
    void* m_handler = nullptr;            // EventHandlerRef
    QHash<int, void*> m_registered;       // id → EventHotKeyRef
};

} // namespace ss::mac
