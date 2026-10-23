#include "core/color/ColorFormat.h"

#include <cmath>
#include <numbers>

namespace ss {

void toHsi(QColor color, double& h, double& s, double& i)
{
    const double r = color.redF(), g = color.greenF(), b = color.blueF();
    i = (r + g + b) / 3.0;
    const double minC = std::min({r, g, b});
    s = i > 0 ? 1.0 - minC / i : 0.0;
    const double num = 0.5 * ((r - g) + (r - b));
    const double den = std::sqrt((r - g) * (r - g) + (r - b) * (g - b));
    double theta = den > 1e-9 ? std::acos(std::clamp(num / den, -1.0, 1.0)) * 180.0 / std::numbers::pi : 0.0;
    h = b <= g ? theta : 360.0 - theta;
    if (s < 1e-9)
        h = 0.0;
}

QString formatColor(QColor c, ColorFormat format, bool hexUppercase)
{
    switch (format) {
    case ColorFormat::Rgb:
        return QStringLiteral("rgb(%1, %2, %3)").arg(c.red()).arg(c.green()).arg(c.blue());
    case ColorFormat::Hex: {
        const QString hex = c.name(QColor::HexRgb);
        return hexUppercase ? hex.toUpper() : hex.toLower();
    }
    case ColorFormat::Hsv:
        return QStringLiteral("hsv(%1, %2%, %3%)").arg(std::max(0, c.hsvHue())).arg(qRound(c.hsvSaturationF() * 100)).arg(qRound(c.valueF() * 100));
    case ColorFormat::Hsl:
        return QStringLiteral("hsl(%1, %2%, %3%)").arg(std::max(0, c.hslHue())).arg(qRound(c.hslSaturationF() * 100)).arg(qRound(c.lightnessF() * 100));
    case ColorFormat::Hsi: {
        double h, s, i;
        toHsi(c, h, s, i);
        return QStringLiteral("hsi(%1, %2%, %3%)").arg(qRound(h)).arg(qRound(s * 100)).arg(qRound(i * 100));
    }
    }
    return {};
}

ColorFormat nextColorFormat(ColorFormat format)
{
    switch (format) {
    case ColorFormat::Rgb: return ColorFormat::Hex;
    case ColorFormat::Hex: return ColorFormat::Hsv;
    case ColorFormat::Hsv: return ColorFormat::Hsl;
    case ColorFormat::Hsl: return ColorFormat::Hsi;
    case ColorFormat::Hsi: return ColorFormat::Rgb;
    }
    return ColorFormat::Hex;
}

ColorFormat colorFormatFromName(const QString& name)
{
    const QString n = name.toLower();
    if (n == QLatin1String("rgb"))
        return ColorFormat::Rgb;
    if (n == QLatin1String("hsv"))
        return ColorFormat::Hsv;
    if (n == QLatin1String("hsl"))
        return ColorFormat::Hsl;
    if (n == QLatin1String("hsi"))
        return ColorFormat::Hsi;
    return ColorFormat::Hex;
}

QString colorFormatName(ColorFormat format)
{
    switch (format) {
    case ColorFormat::Rgb: return QStringLiteral("rgb");
    case ColorFormat::Hex: return QStringLiteral("hex");
    case ColorFormat::Hsv: return QStringLiteral("hsv");
    case ColorFormat::Hsl: return QStringLiteral("hsl");
    case ColorFormat::Hsi: return QStringLiteral("hsi");
    }
    return QStringLiteral("hex");
}

} // namespace ss
