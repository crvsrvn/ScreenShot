#include "app/OutputActions.h"

#include "core/config/Settings.h"
#include "core/output/FileNaming.h"

#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImageWriter>
#include <QMimeData>
#include <QPageSize>
#include <QPainter>
#include <QBuffer>
#include <QPdfWriter>
#include <QPrintDialog>
#include <QPrinter>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>

namespace ss {

OutputActions::OutputActions(Settings& settings, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
{
}

void OutputActions::setSource(const QString& windowTitle, const QString& processName)
{
    m_windowTitle = windowTitle;
    m_processName = processName;
}

QString OutputActions::clipboardFileDir() const
{
    return QDir::temp().filePath(QStringLiteral("ScreenShot-clipboard"));
}

bool OutputActions::copy(const QImage& image)
{
    if (image.isNull()) {
        emit failed(Kind::Copy, tr("Nothing to copy"));
        return false;
    }
    auto* mime = new QMimeData;
    mime->setImageData(image);
    // 复制为文件（OUT-02）：off / auto（有透明像素时）/ png / jpg / webp
    const QString mode = m_settings.stringValue(QStringLiteral("output.copyAsFile"));
    QString format;
    if (mode == QLatin1String("auto") && hasTranslucentPixels(image))
        format = QStringLiteral("png");
    else if (mode != QLatin1String("off") && mode != QLatin1String("auto"))
        format = mode;
    QString filePath;
    if (!format.isEmpty()) {
        QDir().mkpath(clipboardFileDir());
        filePath = uniqueFilePath(clipboardFileDir(), makeFileName(image), format);
        if (writeImage(image, filePath, format))
            mime->setUrls({QUrl::fromLocalFile(filePath)});
        else
            filePath.clear();
    }
    QGuiApplication::clipboard()->setMimeData(mime);
    emit done(Kind::Copy, filePath);
    return true;
}

QString OutputActions::defaultSaveDir() const
{
    const QString dir = m_settings.stringValue(QStringLiteral("output.saveDir"));
    if (!dir.isEmpty() && QDir(dir).exists())
        return dir;
    return QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
}

QString OutputActions::quickSaveDir() const
{
    const QString dir = m_settings.stringValue(QStringLiteral("output.quickSaveDir"));
    return dir.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) : dir;
}

QString OutputActions::autoSaveDir() const
{
    const QString dir = m_settings.stringValue(QStringLiteral("output.autoSaveDir"));
    return dir.isEmpty() ? QDir(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)).filePath(QStringLiteral("ScreenShot")) : dir;
}

QString OutputActions::makeFileName(const QImage& image) const
{
    FileNameContext ctx;
    ctx.time = QDateTime::currentDateTime();
    ctx.imageSize = image.size();
    ctx.sequence = m_sequence;
    ctx.windowTitle = m_windowTitle;
    ctx.processName = m_processName;
    return expandFileNamePattern(m_settings.stringValue(QStringLiteral("output.fileNamePattern")), ctx);
}

bool OutputActions::hasTranslucentPixels(const QImage& image)
{
    if (!image.hasAlphaChannel())
        return false;
    const QImage a = image.format() == QImage::Format_ARGB32 ? image : image.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < a.height(); ++y) {
        const auto* line = reinterpret_cast<const QRgb*>(a.constScanLine(y));
        for (int x = 0; x < a.width(); ++x)
            if (qAlpha(line[x]) != 255)
                return true;
    }
    return false;
}

QStringList OutputActions::saveFormats()
{
    QStringList formats{QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("bmp"), QStringLiteral("webp"), QStringLiteral("pdf")};
    if (QImageWriter::supportedImageFormats().contains("avif"))
        formats.append(QStringLiteral("avif")); // 装了 AVIF 插件时可用（OUT-04）
    return formats;
}

QByteArray OutputActions::encode(const QImage& image, const QString& format, int quality) const
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    QImageWriter writer(&buffer, format.toLower().toLatin1());
    if (quality >= 0)
        writer.setQuality(quality);
    else if (format != QLatin1String("png"))
        writer.setQuality(m_settings.boolValue(QStringLiteral("output.qualityAuto")) ? 90 : m_settings.intValue(QStringLiteral("output.jpegQuality")));
    writer.write(image.hasAlphaChannel() && format == QLatin1String("jpg") ? image.convertToFormat(QImage::Format_RGB32) : image);
    return bytes;
}

void OutputActions::afterSave(const QImage& image, const QString& path)
{
    if (m_settings.boolValue(QStringLiteral("output.copyAfterSave")))
        QGuiApplication::clipboard()->setImage(image); // OUT-12
    const QString command = m_settings.stringValue(QStringLiteral("output.postCommand")).trimmed();
    if (!command.isEmpty()) {
        // OUT-14：{path} 换成文件路径，交给系统 shell
        const QString line = QString(command).replace(QStringLiteral("{path}"), QStringLiteral("\"%1\"").arg(QDir::toNativeSeparators(path)));
#if defined(Q_OS_WIN)
        QProcess process;
        process.setProgram(QStringLiteral("cmd.exe"));
        process.setNativeArguments(QStringLiteral("/c ") + line); // 原样交给 cmd，保留重定向与引号
        process.startDetached();
#else
        QProcess::startDetached(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), line});
#endif
    }
}

bool OutputActions::writeImage(const QImage& image, const QString& path, const QString& format) const
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    const QString f = format.toLower();
    if (f == QLatin1String("pdf")) {
        // OUT-04：PDF，页边距与分页
        QPdfWriter pdf(path);
        pdf.setPageSize(QPageSize(QPageSize::A4));
        const int margin = m_settings.intValue(QStringLiteral("output.pdfMarginMm"));
        pdf.setPageMargins(QMarginsF(margin, margin, margin, margin), QPageLayout::Millimeter);
        pdf.setResolution(150);
        QPainter p(&pdf);
        if (!p.isActive())
            return false;
        const QRect page = p.viewport();
        const double scale = static_cast<double>(page.width()) / image.width();
        const int rowsPerPage = static_cast<int>(page.height() / scale);
        const bool paginate = m_settings.boolValue(QStringLiteral("output.pdfPaginate")) && image.height() * scale > page.height() && rowsPerPage > 0;
        if (!paginate) {
            QSize size = image.size();
            size.scale(page.size(), Qt::KeepAspectRatio);
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            p.drawImage(QRect(page.topLeft(), size), image);
        } else {
            for (int y = 0; y < image.height(); y += rowsPerPage) {
                if (y > 0)
                    pdf.newPage();
                const QImage part = image.copy(0, y, image.width(), std::min(rowsPerPage, image.height() - y));
                p.setRenderHint(QPainter::SmoothPixmapTransform);
                p.drawImage(QRect(page.left(), page.top(), page.width(), static_cast<int>(part.height() * scale)), part);
            }
        }
        return true;
    }
    QImageWriter writer(path, f.toLatin1());
    if (m_settings.boolValue(QStringLiteral("output.metadata"))) {
        // OUT-13：元数据
        if (!m_windowTitle.isEmpty())
            writer.setText(QStringLiteral("Source"), m_windowTitle);
        if (!m_processName.isEmpty())
            writer.setText(QStringLiteral("Process"), m_processName);
        writer.setText(QStringLiteral("CreationTime"), QDateTime::currentDateTime().toString(Qt::ISODate));
        writer.setText(QStringLiteral("Software"), QStringLiteral("ScreenShot"));
    }
    if (f == QLatin1String("jpg") || f == QLatin1String("jpeg") || f == QLatin1String("webp")) {
        // 质量：自动（JPEG 92 / WebP 90）或指定（OUT-05）
        const bool autoQuality = m_settings.boolValue(QStringLiteral("output.qualityAuto"));
        writer.setQuality(autoQuality ? (f == QLatin1String("webp") ? 90 : 92) : m_settings.intValue(QStringLiteral("output.jpegQuality")));
    }
    QImage out = image;
    const bool opaqueFormat = f == QLatin1String("jpg") || f == QLatin1String("jpeg") || f == QLatin1String("bmp");
    if (opaqueFormat && image.hasAlphaChannel()) {
        out = QImage(image.size(), QImage::Format_RGB32);
        out.fill(Qt::white);
        QPainter p(&out);
        p.drawImage(0, 0, image);
    } else if (image.hasAlphaChannel() && !hasTranslucentPixels(image)) {
        out = image.convertToFormat(QImage::Format_RGB32); // 没有半透明像素：去掉透明通道（OUT-05）
    }
    return writer.write(out);
}

QString OutputActions::saveWithDialog(const QImage& image, QWidget* parent)
{
    if (image.isNull())
        return {};
    QString format = m_settings.stringValue(QStringLiteral("output.saveFormat"));
    QString path;
    if (!m_dialogOverride.isEmpty()) {
        path = m_dialogOverride;
    } else {
        const QString suggested = QDir(defaultSaveDir()).filePath(makeFileName(image) + QLatin1Char('.') + format);
        QStringList filters{tr("PNG image (*.png)"), tr("JPEG image (*.jpg *.jpeg)"), tr("BMP image (*.bmp)"), tr("WebP image (*.webp)"), tr("PDF document (*.pdf)")};
        QStringList formats{QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("bmp"), QStringLiteral("webp"), QStringLiteral("pdf")};
        if (saveFormats().contains(QStringLiteral("avif"))) {
            filters.append(tr("AVIF image (*.avif)"));
            formats.append(QStringLiteral("avif"));
        }
        QString selected = filters.value(std::max(0, static_cast<int>(formats.indexOf(format))));
        path = QFileDialog::getSaveFileName(parent, tr("Save screenshot"), suggested, filters.join(QStringLiteral(";;")), &selected);
        if (path.isEmpty())
            return {};
        const int idx = static_cast<int>(filters.indexOf(selected));
        if (idx >= 0)
            format = formats[idx];
        if (QFileInfo(path).suffix().isEmpty())
            path += QLatin1Char('.') + format;
    }
    const QString ext = QFileInfo(path).suffix().toLower();
    if (!ext.isEmpty())
        format = ext == QLatin1String("jpeg") ? QStringLiteral("jpg") : ext;
    if (!writeImage(image, path, format)) {
        emit failed(Kind::Save, tr("Could not write %1").arg(path));
        return {};
    }
    m_settings.setValue(QStringLiteral("output.saveDir"), QFileInfo(path).absolutePath());
    if (format != QLatin1String("pdf"))
        m_settings.setValue(QStringLiteral("output.saveFormat"), format);
    afterSave(image, path);
    emit done(Kind::Save, path);
    return path;
}

QString OutputActions::quickSave(const QImage& image)
{
    if (image.isNull())
        return {};
    const QString format = m_settings.stringValue(QStringLiteral("output.saveFormat"));
    ++m_sequence;
    const QString path = uniqueFilePath(quickSaveDir(), makeFileName(image), format);
    if (!writeImage(image, path, format)) {
        emit failed(Kind::QuickSave, tr("Could not write %1").arg(path));
        return {};
    }
    afterSave(image, path);
    emit done(Kind::QuickSave, path);
    return path;
}

QString OutputActions::autoSave(const QImage& image)
{
    if (image.isNull() || !m_settings.boolValue(QStringLiteral("output.autoSave")))
        return {};
    const QString format = m_settings.stringValue(QStringLiteral("output.saveFormat"));
    const QString path = uniqueFilePath(autoSaveDir(), makeFileName(image), format);
    if (!writeImage(image, path, format)) {
        emit failed(Kind::AutoSave, tr("Could not write %1").arg(path));
        return {};
    }
    emit done(Kind::AutoSave, path);
    return path;
}

int OutputActions::cleanupAutoSave()
{
    const int days = m_settings.intValue(QStringLiteral("output.autoSaveKeepDays"));
    if (days <= 0)
        return 0;
    const QDateTime cutoff = QDateTime::currentDateTime().addDays(-days);
    int removed = 0;
    QDir dir(autoSaveDir());
    for (const QFileInfo& info : dir.entryInfoList({QStringLiteral("*.png"), QStringLiteral("*.jpg"), QStringLiteral("*.bmp"), QStringLiteral("*.webp")}, QDir::Files)) {
        if (info.lastModified() < cutoff && QFile::remove(info.absoluteFilePath()))
            ++removed;
    }
    return removed;
}

bool OutputActions::print(const QImage& image, QWidget* parent, const QString& pdfPath)
{
    if (image.isNull())
        return false;
    QPrinter printer(QPrinter::HighResolution);
    if (!pdfPath.isEmpty()) {
        printer.setOutputFormat(QPrinter::PdfFormat);
        printer.setOutputFileName(pdfPath);
    } else {
        QPrintDialog dialog(&printer, parent);
        if (dialog.exec() != QDialog::Accepted)
            return false;
    }
    QPainter painter(&printer);
    if (!painter.isActive()) {
        emit failed(Kind::Print, tr("Could not start printing"));
        return false;
    }
    // 等比缩放到页面内，居中
    const QRect page = painter.viewport();
    QSize size = image.size();
    size.scale(page.size(), Qt::KeepAspectRatio);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(QRect(page.x() + (page.width() - size.width()) / 2, page.y() + (page.height() - size.height()) / 2, size.width(), size.height()), image);
    painter.end();
    emit done(Kind::Print, pdfPath);
    return true;
}

} // namespace ss
