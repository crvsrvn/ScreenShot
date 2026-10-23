#include "app/OutputActions.h"

#include "core/config/Settings.h"
#include "core/output/FileNaming.h"

#include <QClipboard>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImageWriter>
#include <QPainter>
#include <QStandardPaths>

namespace ss {

OutputActions::OutputActions(Settings& settings, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
{
}

bool OutputActions::copy(const QImage& image)
{
    if (image.isNull()) {
        emit failed(Kind::Copy, tr("Nothing to copy"));
        return false;
    }
    QGuiApplication::clipboard()->setImage(image);
    emit done(Kind::Copy, QString());
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

QString OutputActions::makeFileName(const QImage& image) const
{
    FileNameContext ctx;
    ctx.time = QDateTime::currentDateTime();
    ctx.imageSize = image.size();
    ctx.sequence = m_sequence;
    return expandFileNamePattern(m_settings.stringValue(QStringLiteral("output.fileNamePattern")), ctx);
}

bool OutputActions::writeImage(const QImage& image, const QString& path, const QString& format) const
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QImageWriter writer(path, format.toLatin1());
    const QString f = format.toLower();
    if (f == QLatin1String("jpg") || f == QLatin1String("jpeg") || f == QLatin1String("webp"))
        writer.setQuality(m_settings.intValue(QStringLiteral("output.jpegQuality")));
    // JPEG / BMP 不支持透明：合成到白底
    QImage out = image;
    if ((f == QLatin1String("jpg") || f == QLatin1String("jpeg") || f == QLatin1String("bmp")) && image.hasAlphaChannel()) {
        out = QImage(image.size(), QImage::Format_RGB32);
        out.fill(Qt::white);
        QPainter p(&out);
        p.drawImage(0, 0, image);
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
    // 记住目录与格式
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

} // namespace ss
