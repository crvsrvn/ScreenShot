#pragma once

#include <QOperatingSystemVersion>

#include <QString>
#include <QStringList>

namespace ss::win {

// 支持的最低系统版本（NFR-11）：Windows 10 2004（19041）
QOperatingSystemVersion minimumOsVersion();
bool osSupported();

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

// 进程优先级（GEN-10）：normal | aboveNormal | high
bool setProcessPriority(const QString& level);

// 以管理员身份运行（GEN-05）
bool isElevated();
// 用 UAC 提权重新启动自己（带同样的参数）；用户拒绝或失败返回 false
bool relaunchElevated(const QStringList& args);
// 管理员模式的开机自启：计划任务（最高权限、登录时触发）。注册表 Run 项无法提权启动
bool setAutostartElevated(bool enabled, const QString& appPath, const QString& taskName);
// 上面计划任务用的 schtasks 参数（便于测试）
QStringList autostartTaskArguments(bool enabled, const QString& appPath, const QString& taskName);

} // namespace ss::win
