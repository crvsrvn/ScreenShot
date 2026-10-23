#include "PipeClient.h"

#include <QJsonDocument>
#include <QThread>

#include <windows.h>

namespace ss::driver {

PipeClient::~PipeClient()
{
    close();
}

bool PipeClient::connect(const QString& pipeName, int timeoutMs)
{
    const QString path = QStringLiteral("\\\\.\\pipe\\screenshot-test-") + pipeName;
    const auto deadline = GetTickCount64() + static_cast<ULONGLONG>(timeoutMs);
    for (;;) {
        HANDLE h = CreateFileW(reinterpret_cast<const wchar_t*>(path.utf16()), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                               OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            m_handle = h;
            return true;
        }
        if (GetTickCount64() > deadline)
            return false;
        QThread::msleep(50);
    }
}

void PipeClient::close()
{
    if (m_handle) {
        CloseHandle(static_cast<HANDLE>(m_handle));
        m_handle = nullptr;
    }
}

QJsonObject PipeClient::call(const QString& cmd, QJsonObject args)
{
    args[QStringLiteral("cmd")] = cmd;
    return call(args);
}

QJsonObject PipeClient::call(const QJsonObject& request)
{
    QJsonObject failure{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("管道未连接")}};
    if (!m_handle)
        return failure;
    const QByteArray out = QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
    DWORD written = 0;
    if (!WriteFile(static_cast<HANDLE>(m_handle), out.constData(), static_cast<DWORD>(out.size()), &written, nullptr)) {
        failure[QStringLiteral("error")] = QStringLiteral("写入管道失败");
        return failure;
    }
    char chunk[4096];
    DWORD n = 0;
    for (;;) {
        const size_t nl = m_buffer.find('\n');
        if (nl != std::string::npos) {
            const std::string line = m_buffer.substr(0, nl);
            m_buffer.erase(0, nl + 1);
            const QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(line));
            return doc.isObject() ? doc.object() : failure;
        }
        if (!ReadFile(static_cast<HANDLE>(m_handle), chunk, sizeof(chunk), &n, nullptr) || n == 0) {
            failure[QStringLiteral("error")] = QStringLiteral("读取管道失败（程序退出？）");
            return failure;
        }
        m_buffer.append(chunk, n);
    }
}

} // namespace ss::driver
