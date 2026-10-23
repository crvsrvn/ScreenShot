#pragma once

#include <QColor>
#include <QString>
#include <QStringList>

namespace ss {

// 颜色文本格式（CAP-23）：RGB、HEX、HSV、HSL、HSI
enum class ColorFormat { Rgb, Hex, Hsv, Hsl, Hsi };

QString formatColor(QColor color, ColorFormat format, bool hexUppercase = true);
ColorFormat nextColorFormat(ColorFormat format); // Shift 轮换
ColorFormat colorFormatFromName(const QString& name); // rgb / hex / hsv / hsl / hsi
QString colorFormatName(ColorFormat format);

// HSI 分量（色相 0~360，饱和度与强度 0~1）
void toHsi(QColor color, double& h, double& s, double& i);

} // namespace ss
