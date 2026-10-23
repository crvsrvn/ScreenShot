#pragma once

#include <QImage>
#include <QList>
#include <QRect>
#include <QString>

namespace ss {

// 二维码 / 条码识别（REC-06）：本地 ZXing-C++，无网络
struct CodeResult {
    QString text;
    QString format; // "QRCode"、"DataMatrix"、"Code128" …
    QRect bounds;   // 相对输入图像的像素矩形
    bool isUrl() const;
};

QList<CodeResult> decodeCodes(const QImage& image);
// 生成二维码（测试与“复制为二维码”用）：module 为每个模块的像素数
QImage encodeQrCode(const QString& text, int module = 4, int quietZone = 4);
// 生成任意支持的码（测试用）：format 为 ZXing 名称，如 Code128、EAN13、UPCA、Code39、DataMatrix、PDF417
QImage encodeBarcode(const QString& text, const QString& format, int module = 3, int quietZone = 8);

} // namespace ss
