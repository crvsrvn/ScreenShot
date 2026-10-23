#include "core/ocr/OcrFormat.h"

#include <QStringList>

namespace ss {

OcrLayout ocrLayoutFromName(const QString& name)
{
    if (name == QLatin1String("noNewlines"))
        return OcrLayout::NoNewlines;
    if (name == QLatin1String("multiline"))
        return OcrLayout::Multiline;
    return OcrLayout::Auto;
}

OcrPunctuation ocrPunctuationFromName(const QString& name)
{
    if (name == QLatin1String("halfWidth"))
        return OcrPunctuation::HalfWidth;
    if (name == QLatin1String("fullWidth"))
        return OcrPunctuation::FullWidth;
    return OcrPunctuation::Keep;
}

namespace {

bool isCjk(QChar c)
{
    const ushort u = c.unicode();
    return (u >= 0x4E00 && u <= 0x9FFF) || (u >= 0x3040 && u <= 0x30FF) || (u >= 0xAC00 && u <= 0xD7AF) || (u >= 0x3400 && u <= 0x4DBF)
        || (u >= 0x3000 && u <= 0x303F) || (u >= 0xFF00 && u <= 0xFFEF); // 中日韩标点与全角字符
}

// 两行拼接：两侧都是中日韩文字时不加空格
QString joinLines(const QString& a, const QString& b)
{
    if (a.isEmpty())
        return b;
    if (b.isEmpty())
        return a;
    const bool noSpace = isCjk(a.back()) && isCjk(b.front());
    return noSpace ? a + b : a + QLatin1Char(' ') + b;
}

// 半角 ↔ 全角标点对照
const QList<QPair<QChar, QChar>>& punctuationPairs()
{
    static const QList<QPair<QChar, QChar>> pairs{
        {QLatin1Char(','), QChar(0xFF0C)}, {QLatin1Char('.'), QChar(0x3002)}, {QLatin1Char('?'), QChar(0xFF1F)}, {QLatin1Char('!'), QChar(0xFF01)},
        {QLatin1Char(':'), QChar(0xFF1A)}, {QLatin1Char(';'), QChar(0xFF1B)}, {QLatin1Char('('), QChar(0xFF08)}, {QLatin1Char(')'), QChar(0xFF09)},
        {QLatin1Char('['), QChar(0x3010)}, {QLatin1Char(']'), QChar(0x3011)}, {QLatin1Char('<'), QChar(0x300A)}, {QLatin1Char('>'), QChar(0x300B)},
    };
    return pairs;
}

} // namespace

QString convertPunctuation(const QString& text, OcrPunctuation punctuation)
{
    if (punctuation == OcrPunctuation::Keep)
        return text;
    QString out = text;
    for (const auto& [half, full] : punctuationPairs()) {
        if (punctuation == OcrPunctuation::FullWidth)
            out.replace(half, full);
        else
            out.replace(full, half);
    }
    if (punctuation == OcrPunctuation::HalfWidth) {
        out.replace(QChar(0x201C), QLatin1Char('"'));
        out.replace(QChar(0x201D), QLatin1Char('"'));
        out.replace(QChar(0x2018), QLatin1Char('\''));
        out.replace(QChar(0x2019), QLatin1Char('\''));
    }
    return out;
}

QString formatOcrText(const QList<OcrLine>& lines, OcrLayout layout, OcrPunctuation punctuation)
{
    QStringList texts;
    for (const OcrLine& l : lines)
        texts.append(convertPunctuation(l.text.trimmed(), punctuation));
    if (layout == OcrLayout::Multiline)
        return texts.join(QLatin1Char('\n'));
    if (layout == OcrLayout::NoNewlines) {
        QString out;
        for (const QString& t : texts)
            out = joinLines(out, t);
        return out;
    }
    // Auto：按行距分段
    QStringList paragraphs;
    QString current;
    for (int i = 0; i < lines.size(); ++i) {
        const QString& t = texts[i];
        if (i == 0) {
            current = t;
            continue;
        }
        const QRect& prev = lines[i - 1].bounds;
        const QRect& cur = lines[i].bounds;
        if (prev.isNull() || cur.isNull()) {
            current += QLatin1Char('\n') + t; // 没有位置信息：保持原行
            continue;
        }
        const int lineHeight = std::max(1, std::max(prev.height(), cur.height()));
        const int gap = cur.top() - prev.bottom();
        const bool sameParagraph = gap < lineHeight * 0.8 && std::abs(cur.left() - prev.left()) < lineHeight * 3;
        if (sameParagraph) {
            current = joinLines(current, t);
        } else {
            paragraphs.append(current);
            current = t;
        }
    }
    if (!current.isEmpty() || !lines.isEmpty())
        paragraphs.append(current);
    return paragraphs.join(QStringLiteral("\n\n"));
}

} // namespace ss
