#pragma once

#include <QColor>
#include <QFont>
#include <QImage>
#include <QList>
#include <QRectF>
#include <QString>
#include <memory>

class QTextDocument;

namespace ss {

// 文本贴图（PIN-04、PIN-05）：把文字 / HTML 渲染成图像。尺寸按 scale（屏幕缩放比例）放大，保证贴图在高分屏上清晰。
struct TextPinOptions {
    QFont font{QStringLiteral("Microsoft YaHei"), 12};
    QColor color = Qt::black;
    QColor background = QColor(255, 255, 220);
    int maxWidth = 600;   // 逻辑像素，超过换行
    int padding = 8;      // 逻辑像素
    qreal scale = 1.0;    // 输出图像 = 逻辑尺寸 × scale
    bool html = false;    // true：按 HTML 排版（保留颜色、字号）；不加载任何外部资源
};

// 文本贴图的排版文档：渲染、命中测试与部分文字选择（PIN-06）。坐标为贴图图像像素。
class TextPinDocument {
public:
    TextPinDocument(const QString& text, const TextPinOptions& options);
    ~TextPinDocument();

    QImage render() const;
    const TextPinOptions& options() const { return m_options; }
    QString plainText() const;                    // 全文纯文本
    QString html() const;                         // 全文 HTML（纯文本来源时为简单包装）
    int length() const;                           // 字符数（光标位置上限）
    int hitTest(QPointF imagePos) const;          // 图像坐标 → 光标位置
    QList<QRectF> selectionRects(int from, int to) const; // 选中范围的高亮矩形（图像坐标）
    QString selectedText(int from, int to) const;
    QString selectedHtml(int from, int to) const;

private:
    QPointF toDoc(QPointF imagePos) const;
    TextPinOptions m_options;
    std::unique_ptr<QTextDocument> m_doc;
    int m_width = 0;  // 逻辑像素
    int m_height = 0;
};

QImage renderTextPin(const QString& text, const TextPinOptions& options);

} // namespace ss
