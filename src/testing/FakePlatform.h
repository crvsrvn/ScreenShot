#pragma once

#include "core/ports/Platform.h"

namespace ss::testing {

class FakeScreenCapture;
class FakeWindowDetector;
class FakeElementDetector;
class FakeHotkey;
class FakeOcr;
class FakeInputSimulator;

// 一组测试替身，并保留各自的具体指针便于测试配置。
struct FakePlatformHandles {
    FakeScreenCapture* screenCapture = nullptr;
    FakeWindowDetector* windowDetector = nullptr;
    FakeElementDetector* elementDetector = nullptr;
    FakeHotkey* hotkey = nullptr;
    FakeOcr* ocr = nullptr;
    FakeInputSimulator* input = nullptr;
};

// 两台显示器、不同缩放比例的默认布局：A 在左（150%），B 在右（100%）。
QList<ScreenInfo> defaultMixedScaleLayout();

Platform makeFakePlatform(FakePlatformHandles* handles = nullptr, QList<ScreenInfo> screens = defaultMixedScaleLayout());

} // namespace ss::testing
