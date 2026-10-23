#include "platform/windows/WinPipeServer.h"

#include <windows.h>
#include <string>

namespace ss::win {

QString WinPipeServer::pathFor(const QString& name)
{
    return QStringLiteral("\\\\.\\pipe\\ScreenShot-") + name;
}

WinPipeServer::WinPipeServer(const QString& name, Handler handler)
    : m_pipePath(pathFor(name))
    , m_handler(std::move(handler))
{
}

WinPipeServer::~WinPipeServer()
{
    stop();
}

void WinPipeServer::start()
{
    if (!m_thread.joinable())
        m_thread = std::thread([this] { serve(); });
}

void WinPipeServer::stop()
{
    if (!m_thread.joinable())
        return;
    m_stop = true;
    // 用一次客户端连接唤醒阻塞在 ConnectNamedPipe 的线程
    HANDLE h = CreateFileW(reinterpret_cast<const wchar_t*>(m_pipePath.utf16()), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h != INVALID_HANDLE_VALUE)
        CloseHandle(h);
    m_thread.join();
}

void WinPipeServer::serve()
{
    while (!m_stop) {
        HANDLE pipe = CreateNamedPipeW(reinterpret_cast<const wchar_t*>(m_pipePath.utf16()), PIPE_ACCESS_DUPLEX,
                                       PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                       PIPE_UNLIMITED_INSTANCES, 1 << 20, 1 << 20, 0, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) {
            Sleep(50); // 上一个实例可能还没完全关闭
            continue;
        }
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
                const QByteArray line = QByteArray::fromStdString(buffer.substr(0, nl));
                buffer.erase(0, nl + 1);
                const QByteArray reply = m_handler(line) + '\n';
                DWORD written = 0;
                WriteFile(pipe, reply.constData(), static_cast<DWORD>(reply.size()), &written, nullptr);
                FlushFileBuffers(pipe);
            }
        }
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
}

QByteArray WinPipeServer::sendLine(const QString& name, const QByteArray& line, int timeoutMs)
{
    const QString path = pathFor(name);
    const ULONGLONG deadline = GetTickCount64() + static_cast<ULONGLONG>(timeoutMs);
    HANDLE h = INVALID_HANDLE_VALUE;
    for (;;) {
        h = CreateFileW(reinterpret_cast<const wchar_t*>(path.utf16()), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE)
            break;
        // 忙：实例被占用；找不到：服务线程正在重建下一个实例。都在超时内重试
        const DWORD err = GetLastError();
        if ((err != ERROR_PIPE_BUSY && err != ERROR_FILE_NOT_FOUND) || GetTickCount64() > deadline)
            return {};
        Sleep(20);
    }
    QByteArray result;
    const QByteArray out = line + '\n';
    DWORD n = 0;
    if (WriteFile(h, out.constData(), static_cast<DWORD>(out.size()), &n, nullptr)) {
        char chunk[4096];
        std::string buffer;
        while (buffer.find('\n') == std::string::npos && ReadFile(h, chunk, sizeof(chunk), &n, nullptr) && n > 0)
            buffer.append(chunk, n);
        const size_t nl = buffer.find('\n');
        result = QByteArray::fromStdString(nl == std::string::npos ? buffer : buffer.substr(0, nl));
    }
    CloseHandle(h);
    return result;
}

bool WinPipeServer::exists(const QString& name)
{
    return WaitNamedPipeW(reinterpret_cast<const wchar_t*>(pathFor(name).utf16()), 1) != 0 || GetLastError() == ERROR_PIPE_BUSY;
}

} // namespace ss::win
