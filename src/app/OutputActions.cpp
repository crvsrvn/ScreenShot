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
#include <QPainter>
#include <QBuffer>
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
    QStringList formats{QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("bmp"), QStringLiteral("webp")};
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

void OutputActions::afterSave(const QImage& image)
{
    if (m_settings.boolValue(QStringLiteral("output.copyAfterSave")))
        QGuiApplication::clipboard()->setImage(image); // OUT-12
}

bool OutputActions::writeImage(const QImage& image, const QString& path, const QString& format) const
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    const QString f = format.toLower();
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
        ++m_sequence;
        const QString suggested = QDir(defaultSaveDir()).filePath(makeFileName(image) + QLatin1Char('.') + format);
        QStringList filters{tr("PNG image (*.png)"), tr("JPEG image (*.jpg *.jpeg)"), tr("BMP image (*.bmp)"), tr("WebP image (*.webp)")};
        QStringList formats{QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("bmp"), QStringLiteral("webp")};
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
    m_settings.setValue(QStringLiteral("output.saveFormat"), format);
    afterSave(image);
    emit done(Kind::Save, path);
    return path;
}

} // namespace ss
