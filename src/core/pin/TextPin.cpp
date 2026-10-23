#include "core/pin/TextPin.h"

#include <QAbstractTextDocumentLayout>
#include <QPainter>
#include <QPalette>
#include <QTextDocument>
#include <QTextOption>

namespace ss {

QImage renderTextPin(const QString& text, const TextPinOptions& o)
{
    // 用 QTextDocument 排版：自动换行、多行；不加载任何远程资源（纯文本）
    QTextDocument doc;
    doc.setDefaultFont(o.font);
    QTextOption opt;
    opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    doc.setDefaultTextOption(opt);
    doc.setPlainText(text.isEmpty() ? QStringLiteral(" ") : text);
    doc.setTextWidth(o.maxWidth);
    const QSizeF ideal = doc.size();
    const int w = static_cast<int>(std::ceil(std::min(ideal.width(), static_cast<qreal>(o.maxWidth)))) + 2 * o.padding;
    const int h = static_cast<int>(std::ceil(ideal.height())) + 2 * o.padding;
    doc.setTextWidth(w - 2 * o.padding);

    QImage img(static_cast<int>(std::ceil(w * o.scale)), static_cast<int>(std::ceil(h * o.scale)), QImage::Format_ARGB32_Premultiplied);
    img.fill(o.background);
    QPainter p(&img);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.scale(o.scale, o.scale);
    p.translate(o.padding, o.padding);
    QAbstractTextDocumentLayout::PaintContext ctx;
    ctx.palette.setColor(QPalette::Text, o.color);
    doc.documentLayout()->draw(&p, ctx);
    return img;
}

} // namespace ss
