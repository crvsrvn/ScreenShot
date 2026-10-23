#include "core/pin/TextPin.h"

#include <QAbstractTextDocumentLayout>
#include <QPainter>
#include <QPalette>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QTextLayout>
#include <QTextOption>
#include <QUrl>
#include <cmath>

namespace ss {

namespace {

// 不加载任何资源（图片、样式表）：HTML 贴图只排版文字（PIN-05：不加载远程资源）
class NoResourceDocument : public QTextDocument {
public:
    using QTextDocument::QTextDocument;

protected:
    QVariant loadResource(int, const QUrl&) override { return {}; }
};

} // namespace

TextPinDocument::TextPinDocument(const QString& text, const TextPinOptions& o)
    : m_options(o)
    , m_doc(std::make_unique<NoResourceDocument>())
{
    m_doc->setDefaultFont(o.font);
    QTextOption opt;
    opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    m_doc->setDefaultTextOption(opt);
    if (o.html)
        m_doc->setHtml(text);
    else
        m_doc->setPlainText(text.isEmpty() ? QStringLiteral(" ") : text);
    if (m_doc->isEmpty())
        m_doc->setPlainText(QStringLiteral(" "));
    m_doc->setTextWidth(o.maxWidth);
    const QSizeF ideal = m_doc->size();
    m_width = static_cast<int>(std::ceil(std::min(ideal.width(), static_cast<qreal>(o.maxWidth)))) + 2 * o.padding;
    m_height = static_cast<int>(std::ceil(ideal.height())) + 2 * o.padding;
    m_doc->setTextWidth(m_width - 2 * o.padding);
}

TextPinDocument::~TextPinDocument() = default;

QImage TextPinDocument::render() const
{
    const TextPinOptions& o = m_options;
    QImage img(static_cast<int>(std::ceil(m_width * o.scale)), static_cast<int>(std::ceil(m_height * o.scale)), QImage::Format_ARGB32_Premultiplied);
    img.fill(o.background);
    QPainter p(&img);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.scale(o.scale, o.scale);
    p.translate(o.padding, o.padding);
    QAbstractTextDocumentLayout::PaintContext ctx;
    ctx.palette.setColor(QPalette::Text, o.color);
    m_doc->documentLayout()->draw(&p, ctx);
    return img;
}

QString TextPinDocument::plainText() const
{
    QString s = m_doc->toPlainText();
    s.remove(QChar(0xFFFC)); // 图片等对象占位符
    return s;
}

QString TextPinDocument::html() const
{
    return m_doc->toHtml();
}

int TextPinDocument::length() const
{
    return m_doc->characterCount() - 1;
}

QPointF TextPinDocument::toDoc(QPointF imagePos) const
{
    return imagePos / m_options.scale - QPointF(m_options.padding, m_options.padding);
}

int TextPinDocument::hitTest(QPointF imagePos) const
{
    return std::clamp(m_doc->documentLayout()->hitTest(toDoc(imagePos), Qt::FuzzyHit), 0, length());
}

QList<QRectF> TextPinDocument::selectionRects(int from, int to) const
{
    QList<QRectF> rects;
    if (from > to)
        std::swap(from, to);
    if (from == to)
        return rects;
    for (QTextBlock block = m_doc->begin(); block.isValid(); block = block.next()) {
        const int bStart = block.position();
        const int bEnd = bStart + block.length() - 1;
        if (bEnd < from || bStart > to)
            continue;
        const QTextLayout* layout = block.layout();
        const QRectF blockRect = m_doc->documentLayout()->blockBoundingRect(block);
        for (int i = 0; i < layout->lineCount(); ++i) {
            const QTextLine line = layout->lineAt(i);
            const int lStart = bStart + line.textStart();
            const int lEnd = lStart + line.textLength();
            const int s = std::max(from, lStart);
            const int e = std::min(to, lEnd);
            if (s >= e)
                continue;
            const qreal x1 = line.cursorToX(s - bStart);
            const qreal x2 = line.cursorToX(e - bStart);
            QRectF r(blockRect.left() + layout->position().x() + std::min(x1, x2), blockRect.top() + line.y(), std::abs(x2 - x1), line.height());
            r.translate(m_options.padding, m_options.padding);
            rects.append(QRectF(r.topLeft() * m_options.scale, r.size() * m_options.scale));
        }
    }
    return rects;
}

QString TextPinDocument::selectedText(int from, int to) const
{
    if (from > to)
        std::swap(from, to);
    QTextCursor c(m_doc.get());
    c.setPosition(from);
    c.setPosition(to, QTextCursor::KeepAnchor);
    QString s = c.selectedText();
    s.replace(QChar(0x2029), QLatin1Char('\n'));
    s.remove(QChar(0xFFFC));
    return s;
}

QString TextPinDocument::selectedHtml(int from, int to) const
{
    if (from > to)
        std::swap(from, to);
    QTextCursor c(m_doc.get());
    c.setPosition(from);
    c.setPosition(to, QTextCursor::KeepAnchor);
    return c.selection().toHtml();
}

QImage renderTextPin(const QString& text, const TextPinOptions& options)
{
    return TextPinDocument(text, options).render();
}

} // namespace ss
