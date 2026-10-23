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
#include <QPrintDialog>
#include <QPrinter>
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

bool OutputActions::writeImage(const QImage& image, const QString& path, const QString& format) const
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    const QString f = format.toLower();
    QImageWriter writer(path, f.toLatin1());
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
        const QStringList filters{tr("PNG image (*.png)"), tr("JPEG image (*.jpg *.jpeg)"), tr("BMP image (*.bmp)"), tr("WebP image (*.webp)")};
        const QStringList formats{QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("bmp"), QStringLiteral("webp")};
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
    m_settings.setValue(QStringLiteral("output.saveFormat"), format);
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
