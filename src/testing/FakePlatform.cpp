#include "testing/FakePlatform.h"

#include "testing/FakeElementDetector.h"
#include "testing/FakeHotkey.h"
#include "testing/FakeInputSimulator.h"
#include "testing/FakeOcr.h"
#include "testing/FakeScreenCapture.h"
#include "testing/FakeWindowDetector.h"

namespace ss::testing {

QList<ScreenInfo> defaultMixedScaleLayout()
{
    return {
        ScreenInfo{QStringLiteral("FAKE-A"), QRect(0, 0, 3840, 2160), 1.5, true, false},
        ScreenInfo{QStringLiteral("FAKE-B"), QRect(3840, 0, 3840, 2160), 1.0, false, false},
    };
}

Platform makeFakePlatform(FakePlatformHandles* handles, QList<ScreenInfo> screens)
{
    Platform p;
    auto capture = std::make_unique<FakeScreenCapture>(std::move(screens));
    auto windows = std::make_unique<FakeWindowDetector>();
    auto elements = std::make_unique<FakeElementDetector>();
    auto hotkey = std::make_unique<FakeHotkey>();
    auto ocr = std::make_unique<FakeOcr>();
    auto input = std::make_unique<FakeInputSimulator>();
    if (handles) {
        handles->screenCapture = capture.get();
        handles->windowDetector = windows.get();
        handles->elementDetector = elements.get();
        handles->hotkey = hotkey.get();
        handles->ocr = ocr.get();
        handles->input = input.get();
    }
    p.screenCapture = std::move(capture);
    p.windowDetector = std::move(windows);
    p.elementDetector = std::move(elements);
    p.hotkey = std::move(hotkey);
    p.ocr = std::move(ocr);
    p.input = std::move(input);
    return p;
}

} // namespace ss::testing
