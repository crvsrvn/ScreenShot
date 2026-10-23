#include "platform/windows/WinInputSimulator.h"

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

} // namespace ss::win
