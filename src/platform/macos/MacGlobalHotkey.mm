#include "platform/macos/MacGlobalHotkey.h"

#include "core/util/Clock.h"

#include <Carbon/Carbon.h>

namespace ss::mac {

namespace {

constexpr OSType kSignature = 'SSHK';

OSStatus hotkeyHandler(EventHandlerCallRef, EventRef event, void* userData)
{
    EventHotKeyID hotkeyId{};
    GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID, nullptr, sizeof(hotkeyId), nullptr, &hotkeyId);
    auto* self = static_cast<MacGlobalHotkey*>(userData);
    if (self && hotkeyId.signature == kSignature)
        emit self->triggered(static_cast<int>(hotkeyId.id), monotonicNs());
    return noErr;
}

} // namespace

MacGlobalHotkey::MacGlobalHotkey(QObject* parent)
    : IGlobalHotkey(parent)
{
    EventTypeSpec spec{kEventClassKeyboard, kEventHotKeyPressed};
    EventHandlerRef handler = nullptr;
    InstallApplicationEventHandler(&hotkeyHandler, 1, &spec, this, &handler);
    m_handler = handler;
}

MacGlobalHotkey::~MacGlobalHotkey()
{
    for (void* ref : m_registered)
        UnregisterEventHotKey(static_cast<EventHotKeyRef>(ref));
    if (m_handler)
        RemoveEventHandler(static_cast<EventHandlerRef>(m_handler));
}

bool MacGlobalHotkey::toNative(const QKeySequence& sequence, unsigned& modifiers, unsigned& keyCode)
{
    if (sequence.isEmpty())
        return false;
    const QKeyCombination combo = sequence[0];
    modifiers = 0;
    const Qt::KeyboardModifiers mods = combo.keyboardModifiers();
    // Qt 在 macOS 上把 Cmd 映射为 ControlModifier、Ctrl 映射为 MetaModifier
    if (mods & Qt::ControlModifier)
        modifiers |= cmdKey;
    if (mods & Qt::MetaModifier)
        modifiers |= controlKey;
    if (mods & Qt::ShiftModifier)
        modifiers |= shiftKey;
    if (mods & Qt::AltModifier)
        modifiers |= optionKey;

    const int key = static_cast<int>(combo.key());
    static const QHash<int, unsigned> table{
        {Qt::Key_F1, kVK_F1}, {Qt::Key_F2, kVK_F2}, {Qt::Key_F3, kVK_F3}, {Qt::Key_F4, kVK_F4}, {Qt::Key_F5, kVK_F5},
        {Qt::Key_F6, kVK_F6}, {Qt::Key_F7, kVK_F7}, {Qt::Key_F8, kVK_F8}, {Qt::Key_F9, kVK_F9}, {Qt::Key_F10, kVK_F10},
        {Qt::Key_F11, kVK_F11}, {Qt::Key_F12, kVK_F12}, {Qt::Key_Escape, kVK_Escape}, {Qt::Key_Space, kVK_Space},
        {Qt::Key_Return, kVK_Return}, {Qt::Key_Tab, kVK_Tab}, {Qt::Key_Delete, kVK_ForwardDelete},
        {Qt::Key_A, kVK_ANSI_A}, {Qt::Key_B, kVK_ANSI_B}, {Qt::Key_C, kVK_ANSI_C}, {Qt::Key_D, kVK_ANSI_D}, {Qt::Key_E, kVK_ANSI_E},
        {Qt::Key_F, kVK_ANSI_F}, {Qt::Key_G, kVK_ANSI_G}, {Qt::Key_H, kVK_ANSI_H}, {Qt::Key_I, kVK_ANSI_I}, {Qt::Key_J, kVK_ANSI_J},
        {Qt::Key_K, kVK_ANSI_K}, {Qt::Key_L, kVK_ANSI_L}, {Qt::Key_M, kVK_ANSI_M}, {Qt::Key_N, kVK_ANSI_N}, {Qt::Key_O, kVK_ANSI_O},
        {Qt::Key_P, kVK_ANSI_P}, {Qt::Key_Q, kVK_ANSI_Q}, {Qt::Key_R, kVK_ANSI_R}, {Qt::Key_S, kVK_ANSI_S}, {Qt::Key_T, kVK_ANSI_T},
        {Qt::Key_U, kVK_ANSI_U}, {Qt::Key_V, kVK_ANSI_V}, {Qt::Key_W, kVK_ANSI_W}, {Qt::Key_X, kVK_ANSI_X}, {Qt::Key_Y, kVK_ANSI_Y},
        {Qt::Key_Z, kVK_ANSI_Z}, {Qt::Key_0, kVK_ANSI_0}, {Qt::Key_1, kVK_ANSI_1}, {Qt::Key_2, kVK_ANSI_2}, {Qt::Key_3, kVK_ANSI_3},
        {Qt::Key_4, kVK_ANSI_4}, {Qt::Key_5, kVK_ANSI_5}, {Qt::Key_6, kVK_ANSI_6}, {Qt::Key_7, kVK_ANSI_7}, {Qt::Key_8, kVK_ANSI_8},
        {Qt::Key_9, kVK_ANSI_9},
    };
    auto it = table.find(key);
    if (it == table.end())
        return false; // 非 ANSI 布局的其他字符键：M5 补充（ACT-02）
    keyCode = it.value();
    return true;
}

bool MacGlobalHotkey::registerHotkey(int id, const QKeySequence& sequence)
{
    unsigned mods = 0, keyCode = 0;
    if (!toNative(sequence, mods, keyCode))
        return false;
    unregisterHotkey(id);
    EventHotKeyID hotkeyId{kSignature, static_cast<UInt32>(id)};
    EventHotKeyRef ref = nullptr;
    if (RegisterEventHotKey(keyCode, mods, hotkeyId, GetApplicationEventTarget(), 0, &ref) != noErr)
        return false;
    m_registered.insert(id, ref);
    return true;
}

void MacGlobalHotkey::unregisterHotkey(int id)
{
    if (void* ref = m_registered.take(id))
        UnregisterEventHotKey(static_cast<EventHotKeyRef>(ref));
}

} // namespace ss::mac
