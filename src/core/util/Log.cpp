#include "core/util/Log.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QThread>
#include <cstdio>

namespace ss::log {

namespace {

struct State {
    QMutex mutex;
    Options options;
    QFile file;
    QByteArray pending;
    qint64 lastFlushMs = 0;
    QtMessageHandler previous = nullptr;
    bool installed = false;
};

State& state()
{
    static State s;
    return s;
}

int rank(QtMsgType t)
{
    switch (t) {
    case QtDebugMsg: return 0;
    case QtInfoMsg: return 1;
    case QtWarningMsg: return 2;
    case QtCriticalMsg: return 3;
    case QtFatalMsg: return 4;
    }
    return 1;
}

const char* name(QtMsgType t)
{
    switch (t) {
    case QtDebugMsg: return "DEBUG";
    case QtInfoMsg: return "INFO ";
    case QtWarningMsg: return "WARN ";
    case QtCriticalMsg: return "ERROR";
    case QtFatalMsg: return "FATAL";
    }
    return "?";
}

// 调用方持锁
void flushLocked(State& s)
{
    if (s.pending.isEmpty() || s.options.filePath.isEmpty())
        return;
    if (!s.file.isOpen()) {
        QDir().mkpath(QFileInfo(s.options.filePath).absolutePath());
        s.file.setFileName(s.options.filePath);
        s.file.open(QIODevice::Append | QIODevice::WriteOnly | QIODevice::Text);
    }
    if (s.file.isOpen()) {
        s.file.write(s.pending);
        s.file.flush();
        if (s.file.size() > s.options.maxBytes) {
            // 轮转：当前文件改名为 .1（覆盖旧的）
            s.file.close();
            const QString rotated = s.options.filePath + QStringLiteral(".1");
            QFile::remove(rotated);
            QFile::rename(s.options.filePath, rotated);
        }
    }
    s.pending.clear();
    s.lastFlushMs = QDateTime::currentMSecsSinceEpoch();
}

void handler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    State& s = state();
    QMutexLocker lock(&s.mutex);
    if (rank(type) < rank(s.options.minLevel) && type != QtFatalMsg)
        return;
    const QString line = QStringLiteral("%1 %2 [%3] %4%5\n")
                             .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")))
                             .arg(QLatin1String(name(type)))
                             .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()), 0, 16)
                             .arg(context.category && qstrcmp(context.category, "default") != 0 ? QString::fromLatin1(context.category) + QStringLiteral(": ") : QString())
                             .arg(message);
    s.pending.append(line.toUtf8());
    if (s.options.alsoStderr) {
        std::fputs(line.toLocal8Bit().constData(), stderr);
        std::fflush(stderr);
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (type >= QtWarningMsg || now - s.lastFlushMs >= s.options.flushIntervalMs || s.pending.size() > 64 * 1024)
        flushLocked(s);
}

} // namespace

void install(const Options& options)
{
    State& s = state();
    QMutexLocker lock(&s.mutex);
    s.options = options;
    if (s.file.isOpen())
        s.file.close();
    if (!s.installed) {
        s.previous = qInstallMessageHandler(handler);
        s.installed = true;
    }
}

void setMinLevel(QtMsgType level)
{
    State& s = state();
    QMutexLocker lock(&s.mutex);
    s.options.minLevel = level;
}

void setMaxBytes(qint64 bytes)
{
    State& s = state();
    QMutexLocker lock(&s.mutex);
    s.options.maxBytes = bytes;
}

void flush()
{
    State& s = state();
    QMutexLocker lock(&s.mutex);
    flushLocked(s);
}

void uninstall()
{
    State& s = state();
    QMutexLocker lock(&s.mutex);
    flushLocked(s);
    if (s.installed) {
        qInstallMessageHandler(s.previous);
        s.installed = false;
    }
    if (s.file.isOpen())
        s.file.close();
}

QtMsgType levelFromName(const QString& n)
{
    const QString v = n.toLower();
    if (v == QLatin1String("debug"))
        return QtDebugMsg;
    if (v == QLatin1String("warning"))
        return QtWarningMsg;
    if (v == QLatin1String("error"))
        return QtCriticalMsg;
    return QtInfoMsg;
}

} // namespace ss::log
