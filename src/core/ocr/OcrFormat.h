#pragma once

#include "core/ports/IOcrEngine.h"

#include <QString>

namespace ss {

// OCR 结果排版（REC-03）
enum class OcrLayout { Auto, NoNewlines, Multiline };
enum class OcrPunctuation { Keep, HalfWidth, FullWidth };

OcrLayout ocrLayoutFromName(const QString& name);
OcrPunctuation ocrPunctuationFromName(const QString& name);

// 把识别行按排版规则合成文本：
// - Auto：相邻且行距正常的行视为同一段，按语言用空格或直接相连；段之间空一行
// - NoNewlines：全部合成一行
// - Multiline：每行一行
QString formatOcrText(const QList<OcrLine>& lines, OcrLayout layout, OcrPunctuation punctuation);
QString convertPunctuation(const QString& text, OcrPunctuation punctuation);

} // namespace ss
