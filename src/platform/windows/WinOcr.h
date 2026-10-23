#pragma once

#include "core/ports/IOcrEngine.h"

namespace ss::win {

// Windows.Media.Ocr（系统自带、离线）。经 C++/WinRT 调用；识别在 MTA 工作线程同步完成（R-11）。
class WinOcr : public IOcrEngine {
public:
    bool available() const override;
    OcrResult recognize(const QImage& image) override;
    void configure(const QString& key, const QString& value) override;

    // 系统可用的识别语言标签（如 zh-Hans-CN、en-US）
    static QStringList availableLanguages();

private:
    QString m_language = QStringLiteral("auto");
    bool m_accurate = false;
};

} // namespace ss::win
