#include "platform/windows/WinInputSimulator.h"

#include "platform/windows/WinGlobalHotkey.h"

#include <QKeySequence>
#include <QList>

#include <windows.h>

namespace ss::win {

void WinInputSimulator::mouseWheelAt(QPoint physical, int notches, bool horizontal)
{
    SetCursorPos(physical.x(), physical.y());
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = horizontal ? MOUSEEVENTF_HWHEEL : MOUSEEVENTF_WHEEL;
    in.mi.mouseData = static_cast<DWORD>(notches * WHEEL_DELTA);
    SendInput(1, &in, sizeof(INPUT));
}

void WinInputSimulator::sendKeyCombo(Qt::KeyboardModifiers modifiers, Qt::Key key)
{
    QList<WORD> keys;
    if (modifiers & Qt::ControlModifier)
        keys.append(VK_CONTROL);
    if (modifiers & Qt::ShiftModifier)
        keys.append(VK_SHIFT);
    if (modifiers & Qt::AltModifier)
        keys.append(VK_MENU);
    unsigned mods = 0, vk = 0;
    if (!WinGlobalHotkey::toNative(QKeySequence(QKeyCombination(Qt::NoModifier, key)), mods, vk))
        return;
    keys.append(static_cast<WORD>(vk));
    QList<INPUT> inputs;
    for (WORD k : keys) {
        INPUT in{};
        in.type = INPUT_KEYBOARD;
        in.ki.wVk = k;
        inputs.append(in);
    }
    for (int i = static_cast<int>(keys.size()) - 1; i >= 0; --i) {
        INPUT in{};
        in.type = INPUT_KEYBOARD;
        in.ki.wVk = keys[i];
        in.ki.dwFlags = KEYEVENTF_KEYUP;
        inputs.append(in);
    }
    SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
}

} // namespace ss::win
