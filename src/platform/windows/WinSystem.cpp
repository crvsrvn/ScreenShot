#include "platform/windows/WinSystem.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSettings>

#include <windows.h>
#include <dbghelp.h>
#include <string>

namespace ss::win {

bool acquireSingleInstance(const QString& name)
{
    const QString mutexName = QStringLiteral("Local\\") + name;
    HANDLE h = CreateMutexW(nullptr, FALSE, reinterpret_cast<const wchar_t*>(mutexName.utf16()));
    if (!h)
        return false;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(h);
        return false;
    }
    return true; // 句柄保留到进程退出
}

namespace {
const QString kRunKey = QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run");
}

bool setAutostart(bool enabled, const QString& appPath, const QString& entryName)
{
    QSettings run(kRunKey, QSettings::NativeFormat);
    if (enabled)
        run.setValue(entryName, QStringLiteral("\"%1\"").arg(QDir::toNativeSeparators(appPath)));
    else
        run.remove(entryName);
    run.sync();
    return run.status() == QSettings::NoError;
}

bool isAutostartEnabled(const QString& entryName)
{
    QSettings run(kRunKey, QSettings::NativeFormat);
    return run.contains(entryName);
}

namespace {

std::wstring g_dumpDir;

LONG WINAPI unhandledException(EXCEPTION_POINTERS* info)
{
    // 崩溃处理里尽量不分配内存：路径预先准备好，只拼时间戳
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%s\\crash-%04d%02d%02d-%02d%02d%02d.dmp", g_dumpDir.c_str(), st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei{};
        mei.ThreadId = GetCurrentThreadId();
        mei.ExceptionPointers = info;
        mei.ClientPointers = FALSE;
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
                          static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithDataSegs | MiniDumpWithThreadInfo),
                          &mei, nullptr, nullptr);
        CloseHandle(file);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

} // namespace

void installCrashHandler(const QString& dumpDir)
{
    QDir().mkpath(dumpDir);
    g_dumpDir = QDir::toNativeSeparators(dumpDir).toStdWString();
    SetUnhandledExceptionFilter(unhandledException);
}

QStringList pendingCrashDumps(const QString& dumpDir)
{
    QDir d(dumpDir);
    QStringList list;
    for (const auto& info : d.entryInfoList({QStringLiteral("crash-*.dmp")}, QDir::Files, QDir::Time))
        list << info.absoluteFilePath();
    return list;
}

QString currentUserName()
{
    wchar_t name[256];
    DWORD len = 256;
    if (GetUserNameW(name, &len))
        return QString::fromWCharArray(name, static_cast<int>(len) - 1);
    return QStringLiteral("user");
}

} // namespace ss::win
