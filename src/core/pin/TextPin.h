#pragma once

#include <QColor>
#include <QFont>
#include <QImage>
#include <QString>

namespace ss {

// 纯文本贴图（PIN-04）：把文字渲染成图像。尺寸按 scale（屏幕缩放比例）放大，保证贴图在高分屏上清晰。
struct TextPinOptions {
    QFont font{QStringLiteral("Microsoft YaHei"), 12};
    QColor color = Qt::black;
    QColor background = QColor(255, 255, 220);
    int maxWidth = 600;   // 逻辑像素，超过换行
    int padding = 8;      // 逻辑像素
    qreal scale = 1.0;    // 输出图像 = 逻辑尺寸 × scale
};

QImage renderTextPin(const QString& text, const TextPinOptions& options);

} // namespace ss
