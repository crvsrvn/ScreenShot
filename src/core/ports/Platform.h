#pragma once

#include "core/ports/IElementDetector.h"
#include "core/ports/IGlobalHotkey.h"
#include "core/ports/IInputSimulator.h"
#include "core/ports/IVideoEncoder.h"
#include "core/ports/IOcrEngine.h"
#include "core/ports/IScreenCapture.h"
#include "core/ports/IWindowDetector.h"

#include <memory>

namespace ss {

// 应用层拿到的全部平台能力。平台层或测试替身各自组装一份。
struct Platform {
    std::unique_ptr<IScreenCapture> screenCapture;
    std::unique_ptr<IWindowDetector> windowDetector;
    std::unique_ptr<IElementDetector> elementDetector;
    std::unique_ptr<IGlobalHotkey> hotkey;
    std::unique_ptr<IOcrEngine> ocr;
    std::unique_ptr<IInputSimulator> input;
    // 录屏（VID-03 / VID-04 / VID-07）：没有实现时为空
    VideoEncoderFactory videoEncoder;
    AudioCaptureFactory audioCapture;
    std::unique_ptr<ICameraCapture> camera;
};

} // namespace ss
