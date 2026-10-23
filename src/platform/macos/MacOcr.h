#pragma once

#include "core/ports/IOcrEngine.h"

namespace ss::mac {

// Vision 文字识别（VNRecognizeTextRequest），系统自带、离线。
class MacOcr : public IOcrEngine {
public:
    bool available() const override { return true; }
    OcrResult recognize(const QImage& image) override;
};

} // namespace ss::mac
