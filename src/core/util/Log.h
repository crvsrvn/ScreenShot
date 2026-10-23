#pragma once

#include <QString>
#include <QtGlobal>

namespace ss::log {

// 日志（GEN-09）：接管 Qt 消息处理，写到文件，按大小轮转（保留一份 .1），按级别过滤。
// 线程安全。flushIntervalMs 内的写入合并后落盘。
struct Options {
    QString filePath;
    QtMsgType minLevel = QtInfoMsg;
    qint64 maxBytes = 2 * 1024 * 1024;
    int flushIntervalMs = 500;
    bool alsoStderr = false;
};

void install(const Options& options);
void setMinLevel(QtMsgType level);
void setMaxBytes(qint64 bytes);
void flush();
void uninstall();

QtMsgType levelFromName(const QString& name); // debug / info / warning / error

} // namespace ss::log
