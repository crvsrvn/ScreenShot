#pragma once

#include <QJsonObject>
#include <QString>

namespace ss::driver {

// 内置测试接口的客户端：命名管道上的行式 JSON
class PipeClient {
public:
    ~PipeClient();
    bool connect(const QString& pipeName, int timeoutMs);
    void close();
    bool isOpen() const { return m_handle != nullptr; }

    // 发送命令并等待应答；失败返回 ok=false 的对象
    QJsonObject call(const QJsonObject& request);
    QJsonObject call(const QString& cmd, QJsonObject args = {});

private:
    void* m_handle = nullptr;
    std::string m_buffer;
};

} // namespace ss::driver
