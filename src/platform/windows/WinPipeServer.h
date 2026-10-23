#pragma once

#include <QByteArray>
#include <QString>
#include <atomic>
#include <functional>
#include <thread>

namespace ss::win {

// 通用的行式命名管道服务（本机进程间通信，不用 Qt Network）：
// 客户端每发一行，handler 在服务线程被调用一次，返回值作为应答行写回。
// 拒绝远程客户端（NFR-10）。单实例命令通道与内置测试接口共用。
class WinPipeServer {
public:
    using Handler = std::function<QByteArray(const QByteArray& line)>;

    WinPipeServer(const QString& name, Handler handler);
    ~WinPipeServer();

    void start();
    void stop();
    QString pipePath() const { return m_pipePath; }

    // 客户端：连接、发送一行、等待一行应答。连接失败返回空。
    static QByteArray sendLine(const QString& name, const QByteArray& line, int timeoutMs);
    // 是否有服务在监听
    static bool exists(const QString& name);

    static QString pathFor(const QString& name);

private:
    void serve();

    QString m_pipePath;
    Handler m_handler;
    std::thread m_thread;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_finished{false};
};

} // namespace ss::win
