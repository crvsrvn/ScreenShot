#pragma once

#include <QDateTime>
#include <QSize>
#include <QString>

namespace ss {

// 路径与文件名变量（OUT-08、OUT-10）。
// 变量：{date} {time} {timestamp} {width} {height} {n} {title} {process} {uuid} {computer} {env:NAME}
// 格式：{date:yyyyMMdd} {time:HHmmss}（QDateTime 格式串）
// 修饰符：{title|trunc:20} 截断到 20 个字符；{process|noext} 去掉扩展名；可连用 {process|noext|trunc:8}
// 模板里的 / 表示子目录（例如 {date:yyyy}/{date:MM}/Shot_{time}），每一段分别清理非法字符。
struct FileNameContext {
    QDateTime time;
    QSize imageSize;
    int sequence = 0;
    QString windowTitle;   // 截图来源窗口标题
    QString processName;   // 来源进程名（含扩展名）
    QString computerName;  // 空则取系统
};

QString expandFileNamePattern(const QString& pattern, const FileNameContext& context);

// 去掉文件名里非法的字符（单段，不含路径分隔符）
QString sanitizeFileName(const QString& name);

// 在目录里找一个不存在的文件名：base.ext、base (2).ext …；base 可含子目录
QString uniqueFilePath(const QString& dir, const QString& baseName, const QString& extension);

} // namespace ss
