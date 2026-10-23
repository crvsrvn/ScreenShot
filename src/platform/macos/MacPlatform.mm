#include "platform/macos/MacPlatform.h"

#include "platform/macos/MacElementDetector.h"
#include "platform/macos/MacGlobalHotkey.h"
#include "platform/macos/MacInputSimulator.h"
#include "platform/macos/MacOcr.h"
#include "platform/macos/MacScreenCapture.h"
#include "platform/macos/MacWindowDetector.h"

namespace ss::mac {

Platform makeMacPlatform()
{
    Platform p;
    p.screenCapture = std::make_unique<MacScreenCapture>();
    p.windowDetector = std::make_unique<MacWindowDetector>();
    p.elementDetector = std::make_unique<MacElementDetector>();
    p.hotkey = std::make_unique<MacGlobalHotkey>();
    p.ocr = std::make_unique<MacOcr>();
    p.input = std::make_unique<MacInputSimulator>();
    return p;
}

} // namespace ss::mac
