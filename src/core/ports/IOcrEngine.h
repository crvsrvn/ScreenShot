#pragma once

#include <QImage>
#include <QList>
#include <QRect>
#include <QString>

namespace ss {

struct OcrLine {
    QString text;
    QRect bounds; // 相对输入图像的像素矩形
};

struct OcrResult {
    bool ok = false;
    QString error;
    QList<OcrLine> lines;

    QString text() const
    {
        QStringList parts;
        for (const auto& l : lines)
            parts << l.text;
        return parts.join(QLatin1Char('\n'));
    }
};

// 本地 OCR（REC-01）。实现只能是离线引擎。
class IOcrEngine {
public:
    virtual ~IOcrEngine() = default;
    virtual bool available() const = 0;
    virtual OcrResult recognize(const QImage& image) = 0;
};

} // namespace ss
