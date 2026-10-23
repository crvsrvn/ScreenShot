#pragma once

#include <QFont>
#include <QIcon>
#include <QImage>
#include <QString>

namespace ss {

// 文件卡片贴图（PIN-08）：非图片文件显示为卡片（图标、文件名、大小、目录）
struct FilePinOptions {
    QFont font{QStringLiteral("Microsoft YaHei"), 11};
    int width = 320;  // 逻辑像素
    int padding = 10;
    qreal scale = 1.0;
};

QString humanFileSize(qint64 bytes);
QImage renderFileCard(const QString& filePath, const QIcon& icon, const FilePinOptions& options);

} // namespace ss
