#pragma once

#include "core/ports/IOcrEngine.h"

namespace ss::testing {

class FakeOcr : public IOcrEngine {
public:
    void setResult(OcrResult result) { m_result = std::move(result); }
    bool available() const override { return true; }
    OcrResult recognize(const QImage&) override { return m_result; }

private:
    OcrResult m_result;
};

} // namespace ss::testing
