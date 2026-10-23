#include "app/testapi/TestServer.h"

#include "app/App.h"
#include "app/testapi/TestCommands.h"
#include "platform/windows/WinPipeServer.h"

#include <QJsonDocument>
#include <QMetaObject>

namespace ss {

TestServer::TestServer(const QString& pipeName, App* app)
    : m_app(app)
    , m_server(std::make_unique<win::WinPipeServer>(QStringLiteral("test-") + pipeName, [this](const QByteArray& line) { return handleLine(line); }))
{
}

TestServer::~TestServer() = default;

void TestServer::start()
{
    m_server->start();
}

QByteArray TestServer::handleLine(const QByteArray& line)
{
    QJsonObject reply;
    const QJsonDocument doc = QJsonDocument::fromJson(line);
    if (!doc.isObject()) {
        reply[QStringLiteral("ok")] = false;
        reply[QStringLiteral("error")] = QStringLiteral("not a JSON object");
    } else {
        const QJsonObject request = doc.object();
        QMetaObject::invokeMethod(m_app, [&] { reply = handleTestCommand(*m_app, request); }, Qt::BlockingQueuedConnection);
    }
    return QJsonDocument(reply).toJson(QJsonDocument::Compact);
}

} // namespace ss
