#include "core/pin/FilePin.h"

#include <QDir>
#include <QFileInfo>
#include <QFontMetrics>
#include <QPainter>
#include <cmath>

namespace ss {

QString humanFileSize(qint64 bytes)
{
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double v = static_cast<double>(bytes);
    int u = 0;
    while (v >= 1024.0 && u < 4) {
        v /= 1024.0;
        ++u;
    }
    return u == 0 ? QStringLiteral("%1 B").arg(bytes) : QStringLiteral("%1 %2").arg(QString::number(v, 'f', v < 10 ? 1 : 0), QLatin1String(units[u]));
}

QImage renderFileCard(const QString& filePath, const QIcon& icon, const FilePinOptions& o)
{
    const QFileInfo info(filePath);
    QFont nameFont = o.font;
    nameFont.setBold(true);
    const QFontMetrics nameFm(nameFont);
    const QFontMetrics fm(o.font);
    const int iconSize = 48;
    const int textLeft = o.padding * 2 + iconSize;
    const int textWidth = o.width - textLeft - o.padding;
    const int h = std::max(iconSize + 2 * o.padding, 3 * fm.lineSpacing() + 2 * o.padding);
    QImage img(static_cast<int>(std::ceil(o.width * o.scale)), static_cast<int>(std::ceil(h * o.scale)), QImage::Format_ARGB32_Premultiplied);
    img.fill(QColor(250, 250, 250));
    QPainter p(&img);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.scale(o.scale, o.scale);
    p.setPen(QColor(200, 200, 200));
    p.drawRect(QRectF(0.5, 0.5, o.width - 1, h - 1));
    const QPixmap pm = icon.pixmap(iconSize, iconSize);
    if (!pm.isNull())
        p.drawPixmap(o.padding, (h - iconSize) / 2, iconSize, iconSize, pm);
    int y = o.padding + nameFm.ascent();
    p.setFont(nameFont);
    p.setPen(QColor(30, 30, 30));
    p.drawText(textLeft, y, nameFm.elidedText(info.fileName(), Qt::ElideMiddle, textWidth));
    y += fm.lineSpacing();
    p.setFont(o.font);
    p.setPen(QColor(90, 90, 90));
    p.drawText(textLeft, y, info.isDir() ? QStringLiteral("Folder") : humanFileSize(info.size()));
    y += fm.lineSpacing();
    p.drawText(textLeft, y, fm.elidedText(QDir::toNativeSeparators(info.absolutePath()), Qt::ElideLeft, textWidth));
    return img;
}

} // namespace ss
