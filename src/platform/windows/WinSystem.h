#pragma once

#include <QString>
#include <QStringList>

namespace ss::win {

// 单实例（GEN-01）：拿到命名互斥体返回 true；已有实例返回 false。句柄保持到进程退出。
bool acquireSingleInstance(const QString& name);

// 开机自启（GEN-04）：HKCU\...\Run
bool setAutostart(bool enabled, const QString& appPath, const QString& entryName);
bool isAutostartEnabled(const QString& entryName);

// 崩溃转储（GEN-09）：未处理异常时用 MiniDumpWriteDump 写到 dumpDir，文件名 crash-<时间>.dmp
void installCrashHandler(const QString& dumpDir);
// dumpDir 下尚未处理的转储文件（按时间排序，最新在前）
QStringList pendingCrashDumps(const QString& dumpDir);

// 当前用户名（用于本机 IPC 名称）
QString currentUserName();

} // namespace ss::win
