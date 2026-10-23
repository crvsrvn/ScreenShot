#include "app/CommandServer.h"

#include "platform/windows/WinPipeServer.h"
#include "platform/windows/WinSystem.h"

#include "core/config/Actions.h"

#include <QMetaObject>
#include <condition_variable>

namespace ss {

struct CommandServer::Pending {
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    QString text;
};

namespace {
constexpr int kWaitTimeoutMs = 10 * 60 * 1000;
}

CommandServer::CommandServer(const QString& channelName, QObject* parent)
    : QObject(parent)
    , m_server(std::make_unique<win::WinPipeServer>(channelName, [this](const QByteArray& line) {
        const QStringList command = splitCommandLine(QString::fromUtf8(line));
        const bool wait = command.contains(QStringLiteral("--wait"));
        std::shared_ptr<Pending> pending;
        int id = 0;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            id = m_nextId++;
            if (wait) {
                pending = std::make_shared<Pending>();
                m_pending[id] = pending;
            }
        }
        QMetaObject::invokeMethod(this, [this, command, id] { emit commandReceived(command, id); }, Qt::QueuedConnection);
        if (!pending)
            return QByteArray("ok");
        // 调用方等待完成（ACT-06）
        std::unique_lock<std::mutex> lock(pending->mutex);
        pending->cv.wait_for(lock, std::chrono::milliseconds(kWaitTimeoutMs), [&] { return pending->done; });
        {
            std::lock_guard<std::mutex> l(m_mutex);
            m_pending.erase(id);
        }
        return pending->done ? pending->text.toUtf8() : QByteArray("timeout");
    }))
{
}

CommandServer::~CommandServer() = default;

void CommandServer::reply(int requestId, const QString& text)
{
    std::shared_ptr<Pending> pending;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (auto it = m_pending.find(requestId); it != m_pending.end())
            pending = it->second;
    }
    if (!pending)
        return;
    std::lock_guard<std::mutex> lock(pending->mutex);
    pending->done = true;
    pending->text = text;
    pending->cv.notify_all();
}

void CommandServer::start()
{
    m_server->start();
}

QString CommandServer::defaultChannelName()
{
    return QStringLiteral("cmd-") + win::currentUserName();
}

bool CommandServer::sendToRunningInstance(const QString& channelName, const QStringList& command, QString* reply)
{
    if (!win::WinPipeServer::exists(channelName))
        return false;
    // 含空格的参数加引号；--wait 时等待更久（截图可能要用户操作）
    QStringList quoted;
    for (const QString& c : command)
        quoted.append(c.contains(QLatin1Char(' ')) ? QLatin1Char('"') + c + QLatin1Char('"') : c);
    const int timeout = command.contains(QStringLiteral("--wait")) ? 10 * 60 * 1000 : 3000;
    const QByteArray r = win::WinPipeServer::sendLine(channelName, quoted.join(QLatin1Char(' ')).toUtf8(), timeout);
    if (reply)
        *reply = QString::fromUtf8(r);
    return !r.isEmpty();
}

} // namespace ss
