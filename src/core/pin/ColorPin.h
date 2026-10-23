#pragma once

#include <QColor>
#include <QFont>
#include <QImage>
#include <QString>
#include <QStringList>
#include <optional>

namespace ss {

// 颜色贴图（PIN-07）：识别文本里的颜色并渲染成色卡
// 支持：#RGB / #RRGGBB / #AARRGGBB、rgb(r, g, b) / rgba()、三个整数（0~255）、三个 0~1 小数、CSS 颜色名
std::optional<QColor> parseColorText(const QString& text);

struct ColorPinOptions {
    QFont font{QStringLiteral("Consolas"), 11};
    int width = 200;   // 逻辑像素
    int swatchHeight = 56;
    int padding = 8;
    qreal scale = 1.0;
};

// 色卡上显示的各种格式（HEX、RGB、HSV、HSL），与 copyColorAs 的格式名一致
struct ColorFormatLine {
    QString name;  // hex / rgb / hsv / hsl / float
    QString text;
};
QList<ColorFormatLine> colorFormatLines(const QColor& color);
QImage renderColorPin(const QColor& color, const ColorPinOptions& options);

} // namespace ss
