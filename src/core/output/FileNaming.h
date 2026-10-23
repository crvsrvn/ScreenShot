#pragma once

#include <QDateTime>
#include <QSize>
#include <QString>

namespace ss {

// 文件名变量展开（OUT-08 快速保存；OUT-10 的基础子集）。
// 支持：{date} {time} {timestamp} {width} {height} {n}；{date:格式} {time:格式} 用 QDateTime 格式串。
struct FileNameContext {
    QDateTime time;
    QSize imageSize;
    int sequence = 0;
};

QString expandFileNamePattern(const QString& pattern, const FileNameContext& context);

// 去掉文件名里非法的字符
QString sanitizeFileName(const QString& name);

// 在目录里找一个不存在的文件名：base.ext、base (2).ext …
QString uniqueFilePath(const QString& dir, const QString& baseName, const QString& extension);

} // namespace ss
