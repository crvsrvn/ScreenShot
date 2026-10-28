// 贴图窗口的内容类功能（PIN-05 ~ PIN-07、PIN-27、PIN-29 ~ PIN-32）：文本 / 颜色内容、文字选择、灰度反色、缩略图、显示范围、裁剪、置顶。
// 与 PinWindow.cpp 同属 PinWindow 类，按功能拆文件。
#include "app/PinWindow.h"

#include "app/WindowChrome.h"
#include "core/color/ColorFormat.h"
#include "core/config/Settings.h"
#include "core/pin/ColorPin.h"
#include "core/pin/FilePin.h"
#include "core/pin/TextPin.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QMimeData>
#include <QProcess>
#include <QUrl>

namespace ss {

// ---- 文本内容（PIN-05、PIN-06）----

void PinWindow::setTextContent(const QString& text, const TextPinOptions& options)
{
    m_kind = ContentKind::Text;
    m_textSource = text;
    m_textOptions = options;
    m_textDoc = std::make_unique<TextPinDocument>(text, options);
    m_textSelection = {0, 0};
    replaceSource(m_textDoc->render());
}

void PinWindow::setIgnoreFormatting(bool ignore)
{
    if (m_kind != ContentKind::Text || !m_textOptions.html == ignore)
        return;
    // 忽略格式：按纯文本重新排版（PIN-05）；恢复格式：按原 HTML
    TextPinOptions o = m_textOptions;
    o.html = !ignore;
    const QString plain = m_textDoc ? m_textDoc->plainText() : m_textSource;
    const QString html = m_textHtmlSource.isEmpty() ? m_textSource : m_textHtmlSource;
    if (ignore)
        m_textHtmlSource = m_textSource;
    setTextContent(ignore ? plain : html, o);
}

QString PinWindow::textPlain() const
{
    return m_textDoc ? m_textDoc->plainText() : QString();
}

QString PinWindow::textHtml() const
{
    return m_textDoc ? m_textDoc->html() : QString();
}

void PinWindow::copyText()
{
    if (m_kind == ContentKind::Image) {
        const QString text = m_ocrSelected.isEmpty() ? [this] {
            QStringList all;
            for (const OcrLine& l : m_ocrLines)
                all.append(l.text);
            return all.join(QLatin1Char('\n'));
        }() : selectedText();
        if (!text.isEmpty())
            QApplication::clipboard()->setText(text);
        return;
    }
    if (!m_textDoc)
        return;
    auto* mime = new QMimeData;
    const auto [from, to] = m_textSelection;
    if (m_textSelecting && from != to) {
        mime->setText(m_textDoc->selectedText(from, to));
        mime->setHtml(m_textDoc->selectedHtml(from, to));
    } else {
        mime->setText(m_textDoc->plainText());
        mime->setHtml(m_textDoc->html());
    }
    QApplication::clipboard()->setMimeData(mime);
}

void PinWindow::setTextSelectionMode(bool on)
{
    if (m_kind == ContentKind::Image) {
        // REC-04：图像贴图先识别文字行
        if (!m_settings.boolValue(QStringLiteral("ocr.pinSelectText")))
            return;
        m_textSelecting = on;
        m_ocrSelected.clear();
        if (on && m_ocrLines.isEmpty())
            emit ocrLinesRequested(this, displayedImage());
        setCursor(on ? Qt::IBeamCursor : Qt::ArrowCursor);
        update();
        return;
    }
    if (m_kind != ContentKind::Text)
        return;
    m_textSelecting = on;
    m_textSelection = {0, 0};
    setCursor(on ? Qt::IBeamCursor : Qt::ArrowCursor);
    update();
}

void PinWindow::setOcrLines(const QList<OcrLine>& lines)
{
    m_ocrLines = lines;
    m_ocrSelected.clear();
    update();
}

void PinWindow::selectOcrLinesIn(const QRectF& imageRect)
{
    m_ocrSelected.clear();
    for (int i = 0; i < m_ocrLines.size(); ++i)
        if (imageRect.intersects(QRectF(m_ocrLines[i].bounds)))
            m_ocrSelected.append(i);
    update();
}

QString PinWindow::selectedText() const
{
    if (m_kind == ContentKind::Image) {
        QStringList parts;
        for (int i : m_ocrSelected)
            parts.append(m_ocrLines[i].text);
        return parts.join(QLatin1Char('\n'));
    }
    const auto [from, to] = m_textSelection;
    return m_textDoc && from != to ? m_textDoc->selectedText(from, to) : QString();
}

void PinWindow::selectTextRange(int from, int to)
{
    if (!m_textDoc)
        return;
    const int n = m_textDoc->length();
    m_textSelection = {std::clamp(from, 0, n), std::clamp(to, 0, n)};
    update();
}

void PinWindow::textSelectAt(QPointF imagePos, bool extend)
{
    if (!m_textDoc)
        return;
    const int pos = m_textDoc->hitTest(imagePos);
    if (extend)
        m_textSelection.second = pos;
    else
        m_textSelection = {pos, pos};
    update();
}

QList<QRectF> PinWindow::textSelectionRectsImage() const
{
    return m_textDoc ? m_textDoc->selectionRects(m_textSelection.first, m_textSelection.second) : QList<QRectF>();
}

// ---- 颜色内容（PIN-07）----

void PinWindow::setColorContent(const QColor& color, qreal scale)
{
    m_kind = ContentKind::Color;
    m_color = color;
    ColorPinOptions o;
    o.scale = scale;
    o.font = QFont(m_settings.stringValue(QStringLiteral("pin.textFont")), 11);
    replaceSource(renderColorPin(color, o));
}

void PinWindow::copyColorAs(const QString& format)
{
    if (m_kind != ContentKind::Color)
        return;
    for (const auto& line : colorFormatLines(m_color)) {
        if (line.name == format) {
            QApplication::clipboard()->setText(line.text);
            return;
        }
    }
}

void PinWindow::editColorValue()
{
    bool ok = false;
    const QString text = theme::getText(this, tr("Edit color"), tr("Color (#hex, rgb(), name, 3 numbers):"), colorFormatLines(m_color).first().text, &ok);
    if (!ok)
        return;
    if (const auto c = parseColorText(text))
        setColorContent(*c, m_textOptions.scale > 0 ? m_textOptions.scale : 1.0);
}

// ---- 文件卡片（PIN-08）----

void PinWindow::setFileContent(const QString& path, qreal scale)
{
    m_kind = ContentKind::File;
    m_filePath = path;
    FilePinOptions o;
    o.scale = scale;
    o.font = QFont(m_settings.stringValue(QStringLiteral("pin.textFont")), 11);
    QFileIconProvider icons;
    replaceSource(renderFileCard(path, icons.icon(QFileInfo(path)), o));
}

void PinWindow::openFile()
{
    if (!m_filePath.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_filePath));
}

void PinWindow::copyFile()
{
    auto* mime = new QMimeData;
    mime->setUrls({QUrl::fromLocalFile(m_filePath)});
    mime->setText(QDir::toNativeSeparators(m_filePath));
    QApplication::clipboard()->setMimeData(mime);
}

void PinWindow::copyFilePath()
{
    QApplication::clipboard()->setText(QDir::toNativeSeparators(m_filePath));
}

std::function<void(const QString&)> PinWindow::revealHandler = [](const QString& path) {
#if defined(Q_OS_WIN)
    QProcess::startDetached(QStringLiteral("explorer.exe"), {QStringLiteral("/select,"), QDir::toNativeSeparators(path)});
#elif defined(Q_OS_MACOS)
    QProcess::startDetached(QStringLiteral("open"), {QStringLiteral("-R"), path});
#else
    Q_UNUSED(path);
#endif
};

void PinWindow::revealFile()
{
    revealHandler(m_filePath);
}

// ---- 换底图（内容替换、裁剪确认共用）----

void PinWindow::replaceSource(const QImage& image)
{
    m_source = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    m_source.setDevicePixelRatio(1.0);
    m_annotated = QImage();
    m_cropRect = QRect();
    m_viewRect = QRectF();
    rebuildTransformed();
    relayout();
}

// ---- 灰度 / 反色（PIN-27）----

void PinWindow::setGrayscale(bool on)
{
    if (m_gray == on)
        return;
    m_gray = on;
    rebuildTransformed();
    update();
}

void PinWindow::setInverted(bool on)
{
    if (m_invert == on)
        return;
    m_invert = on;
    rebuildTransformed();
    update();
}

// ---- 缩略图（PIN-29）----

void PinWindow::toggleThumbnail()
{
    if (m_thumbnail) {
        m_thumbnail = false;
        setScale(m_scaleBeforeThumbnail, false);
        return;
    }
    const int size = std::max(16, m_settings.intValue(QStringLiteral("pin.thumbnailSize")));
    const int longest = std::max(m_display.width(), m_display.height());
    if (longest <= 0)
        return;
    m_scaleBeforeThumbnail = m_scale;
    m_thumbnail = true;
    setScale(static_cast<qreal>(size) / longest, false);
}

// ---- 显示范围（PIN-30）----

void PinWindow::setViewRect(const QRectF& displayRect)
{
    // displayRect 以当前显示图（可能已是局部）为坐标；换算成完整图坐标
    QRectF full = displayRect.translated(m_viewRect.isEmpty() ? QPointF() : m_viewRect.topLeft());
    full = full.intersected(QRectF(QPointF(0, 0), QSizeF(m_transformedFull.size())));
    if (full.width() < 2 || full.height() < 2)
        return;
    m_viewRect = full;
    refreshDisplay();
    relayout();
}

void PinWindow::clearViewRect()
{
    if (m_viewRect.isEmpty())
        return;
    m_viewRect = QRectF();
    refreshDisplay();
    relayout();
}

void PinWindow::panView(QPointF imageDelta)
{
    if (m_viewRect.isEmpty())
        return;
    QRectF r = m_viewRect.translated(imageDelta);
    const QRectF bounds(QPointF(0, 0), QSizeF(m_transformedFull.size()));
    r.moveLeft(std::clamp(r.left(), bounds.left(), bounds.right() - r.width()));
    r.moveTop(std::clamp(r.top(), bounds.top(), bounds.bottom() - r.height()));
    if (r == m_viewRect)
        return;
    m_viewRect = r;
    refreshDisplay();
}

// ---- 裁剪（PIN-31）----

QRect PinWindow::displayToSource(const QRectF& displayRect) const
{
    // 显示图坐标 → 完整变换图坐标 → 原图坐标（逆旋转 / 翻转）
    const QRectF full = displayRect.translated(m_viewRect.isEmpty() ? QPointF() : m_viewRect.topLeft());
    QTransform t;
    t.rotate(m_rotation);
    t.scale(m_flipH ? -1 : 1, m_flipV ? -1 : 1);
    const QImage& base = croppedSource();
    const QTransform real = QImage::trueMatrix(t, base.width(), base.height());
    QRect r = real.inverted().mapRect(full).toAlignedRect().intersected(base.rect());
    if (!m_cropRect.isNull())
        r.translate(m_cropRect.topLeft());
    return r;
}

QImage PinWindow::croppedSource() const
{
    return m_cropRect.isNull() ? m_source : m_source.copy(m_cropRect);
}

void PinWindow::cropTo(const QRectF& displayRect)
{
    const QRect r = displayToSource(displayRect);
    if (r.width() < 2 || r.height() < 2)
        return;
    const QPointF anchor = contentRect().topLeft();
    m_cropRect = r;
    m_viewRect = QRectF();
    rebuildTransformed();
    m_contentTopLeft = anchor;
    relayout();
}

void PinWindow::restoreCrop()
{
    if (m_cropRect.isNull())
        return;
    m_cropRect = QRect();
    rebuildTransformed();
    relayout();
}

void PinWindow::confirmCrop()
{
    if (m_cropRect.isNull())
        return;
    m_source = m_source.copy(m_cropRect);
    if (!m_annotated.isNull())
        m_annotated = m_annotated.copy(m_cropRect);
    m_cropRect = QRect();
    rebuildTransformed();
    relayout();
}

// ---- 置顶（PIN-32）----

void PinWindow::setTopmost(bool on)
{
    if (m_topmost == on)
        return;
    m_topmost = on;
    const bool wasVisible = isVisible();
    // 不置顶时作为普通窗口出现在任务栏 / 程序坞，方便切回
    setWindowFlag(Qt::WindowStaysOnTopHint, on);
    setWindowFlag(Qt::Tool, on);
    setWindowFlag(Qt::Window, !on);
    if (wasVisible)
        show();
    relayout();
}

} // namespace ss
