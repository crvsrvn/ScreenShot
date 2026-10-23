#include "app/testapi/TestServer.h"

#include "app/App.h"
#include "app/testapi/TestCommands.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QMetaObject>

#include <windows.h>
#include <string>

namespace ss {

TestServer::TestServer(const QString& pipeName, App* app)
    : m_pipePath(QStringLiteral("\\\\.\\pipe\\screenshot-test-") + pipeName)
    , m_app(app)
{
}

TestServer::~TestServer()
{
    m_stop = true;
    // 用一次客户端连接唤醒阻塞在 ConnectNamedPipe 的线程
    HANDLE h = CreateFileW(reinterpret_cast<const wchar_t*>(m_pipePath.utf16()), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h != INVALID_HANDLE_VALUE)
        CloseHandle(h);
    if (m_thread.joinable())
        m_thread.join();
}

void TestServer::start()
{
    m_thread = std::thread([this] { serve(); });
}

void TestServer::serve()
{
    while (!m_stop) {
        HANDLE pipe = CreateNamedPipeW(reinterpret_cast<const wchar_t*>(m_pipePath.utf16()), PIPE_ACCESS_DUPLEX,
                                       PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1,
                                       1 << 20, 1 << 20, 0, nullptr);
        if (pipe == INVALID_HANDLE_VALUE)
            return;
        const BOOL connected = ConnectNamedPipe(pipe, nullptr) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
        if (!connected || m_stop) {
            CloseHandle(pipe);
            continue;
        }
        std::string buffer;
        char chunk[4096];
        DWORD n = 0;
        while (!m_stop && ReadFile(pipe, chunk, sizeof(chunk), &n, nullptr) && n > 0) {
            buffer.append(chunk, n);
            size_t nl;
            while ((nl = buffer.find('\n')) != std::string::npos) {
                const std::string line = buffer.substr(0, nl);
                buffer.erase(0, nl + 1);
                QJsonObject reply;
                QJsonObject request;
                const QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(line));
                if (!doc.isObject()) {
                    reply[QStringLiteral("ok")] = false;
                    reply[QStringLiteral("error")] = QStringLiteral("不是 JSON 对象");
                } else {
                    request = doc.object();
                    QMetaObject::invokeMethod(
                        m_app, [&] { reply = dispatch(request); }, Qt::BlockingQueuedConnection);
                }
                const QByteArray out = QJsonDocument(reply).toJson(QJsonDocument::Compact) + '\n';
                DWORD written = 0;
                WriteFile(pipe, out.constData(), static_cast<DWORD>(out.size()), &written, nullptr);
                FlushFileBuffers(pipe);
                if (request_is_quit(request))
                    break;
            }
        }
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
}

QJsonObject TestServer::dispatch(const QJsonObject& request)
{
    return handleTestCommand(*m_app, request);
}

} // namespace ss
