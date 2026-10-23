#include "app/CommandServer.h"

#include "platform/windows/WinPipeServer.h"
#include "platform/windows/WinSystem.h"

#include <QMetaObject>

namespace ss {

CommandServer::CommandServer(const QString& channelName, QObject* parent)
    : QObject(parent)
    , m_server(std::make_unique<win::WinPipeServer>(channelName, [this](const QByteArray& line) {
        const QStringList command = QString::fromUtf8(line).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        QMetaObject::invokeMethod(this, [this, command] { emit commandReceived(command); }, Qt::QueuedConnection);
        return QByteArray("ok");
    }))
{
}

CommandServer::~CommandServer() = default;

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
    const QByteArray r = win::WinPipeServer::sendLine(channelName, command.join(QLatin1Char(' ')).toUtf8(), 3000);
    if (reply)
        *reply = QString::fromUtf8(r);
    return !r.isEmpty();
}

} // namespace ss
