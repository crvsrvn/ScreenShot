#include "core/pin/ColorPin.h"

#include <QFontMetrics>
#include <QPainter>
#include <QRegularExpression>
#include <cmath>

namespace ss {

std::optional<QColor> parseColorText(const QString& raw)
{
    const QString text = raw.trimmed();
    if (text.isEmpty() || text.size() > 64 || text.contains(QLatin1Char('\n')))
        return std::nullopt;
    // #hex 与 CSS 颜色名
    static const QRegularExpression hex(QStringLiteral("^#([0-9a-fA-F]{3}|[0-9a-fA-F]{6}|[0-9a-fA-F]{8})$"));
    if (hex.match(text).hasMatch())
        return QColor::fromString(text);
    static const QRegularExpression name(QStringLiteral("^[A-Za-z]{3,20}$"));
    if (name.match(text).hasMatch()) {
        if (QColor::isValidColorName(text.toLower()))
            return QColor::fromString(text.toLower());
        return std::nullopt;
    }
    // rgb(…) / rgba(…) 或裸的三个数
    static const QRegularExpression triple(QStringLiteral("^(?:rgba?\\s*\\(\\s*)?([0-9.]+)\\s*[, ]\\s*([0-9.]+)\\s*[, ]\\s*([0-9.]+)(?:\\s*[, /]\\s*([0-9.]+%?))?\\s*\\)?$"));
    const auto m = triple.match(text);
    if (!m.hasMatch())
        return std::nullopt;
    const QString a = m.captured(1), b = m.captured(2), c = m.captured(3);
    bool ok1, ok2, ok3;
    const double v1 = a.toDouble(&ok1), v2 = b.toDouble(&ok2), v3 = c.toDouble(&ok3);
    if (!ok1 || !ok2 || !ok3)
        return std::nullopt;
    const bool anyDot = a.contains(QLatin1Char('.')) || b.contains(QLatin1Char('.')) || c.contains(QLatin1Char('.'));
    const bool unit = anyDot && v1 <= 1.0 && v2 <= 1.0 && v3 <= 1.0;
    QColor color;
    if (unit)
        color = QColor::fromRgbF(v1, v2, v3);
    else if (v1 <= 255 && v2 <= 255 && v3 <= 255 && !anyDot)
        color = QColor(static_cast<int>(v1), static_cast<int>(v2), static_cast<int>(v3));
    else
        return std::nullopt;
    const QString alpha = m.captured(4);
    if (!alpha.isEmpty()) {
        if (alpha.endsWith(QLatin1Char('%')))
            color.setAlphaF(std::clamp(alpha.chopped(1).toDouble() / 100.0, 0.0, 1.0));
        else if (alpha.contains(QLatin1Char('.')) || alpha == QLatin1String("1") || alpha == QLatin1String("0"))
            color.setAlphaF(std::clamp(alpha.toDouble(), 0.0, 1.0));
        else
            color.setAlpha(std::clamp(alpha.toInt(), 0, 255));
    }
    return color;
}

QList<ColorFormatLine> colorFormatLines(const QColor& c)
{
    QList<ColorFormatLine> lines;
    lines.append({QStringLiteral("hex"), c.alpha() == 255 ? c.name(QColor::HexRgb).toUpper() : c.name(QColor::HexArgb).toUpper()});
    lines.append({QStringLiteral("rgb"), c.alpha() == 255 ? QStringLiteral("rgb(%1, %2, %3)").arg(c.red()).arg(c.green()).arg(c.blue())
                                                         : QStringLiteral("rgba(%1, %2, %3, %4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(QString::number(c.alphaF(), 'f', 2))});
    lines.append({QStringLiteral("hsv"), QStringLiteral("hsv(%1, %2%, %3%)").arg(std::max(0, c.hsvHue())).arg(qRound(c.hsvSaturationF() * 100)).arg(qRound(c.valueF() * 100))});
    lines.append({QStringLiteral("hsl"), QStringLiteral("hsl(%1, %2%, %3%)").arg(std::max(0, c.hslHue())).arg(qRound(c.hslSaturationF() * 100)).arg(qRound(c.lightnessF() * 100))});
    lines.append({QStringLiteral("float"), QStringLiteral("%1, %2, %3").arg(QString::number(c.redF(), 'f', 3), QString::number(c.greenF(), 'f', 3), QString::number(c.blueF(), 'f', 3))});
    return lines;
}

QImage renderColorPin(const QColor& color, const ColorPinOptions& o)
{
    const QList<ColorFormatLine> lines = colorFormatLines(color);
    const QFontMetrics fm(o.font);
    const int lineH = fm.lineSpacing();
    const int h = o.swatchHeight + o.padding * 2 + lineH * static_cast<int>(lines.size());
    QImage img(static_cast<int>(std::ceil(o.width * o.scale)), static_cast<int>(std::ceil(h * o.scale)), QImage::Format_ARGB32_Premultiplied);
    img.fill(QColor(250, 250, 250));
    QPainter p(&img);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.scale(o.scale, o.scale);
    // 色块：半透明颜色下面铺棋盘格
    if (color.alpha() < 255) {
        for (int y = 0; y < o.swatchHeight; y += 8)
            for (int x = 0; x < o.width; x += 8)
                p.fillRect(QRect(x, y, 8, 8), ((x / 8 + y / 8) % 2) ? QColor(200, 200, 200) : QColor(255, 255, 255));
    }
    p.fillRect(QRect(0, 0, o.width, o.swatchHeight), color);
    p.setFont(o.font);
    p.setPen(QColor(40, 40, 40));
    int y = o.swatchHeight + o.padding + fm.ascent();
    for (const auto& line : lines) {
        p.drawText(o.padding, y, line.text);
        y += lineH;
    }
    return img;
}

} // namespace ss
