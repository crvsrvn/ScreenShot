#pragma once

#include <QJsonObject>
#include <QString>
#include <atomic>
#include <thread>

namespace ss {

class App;

// 内置测试接口（TST-03、NFR-10）：只编译进测试构建。
// Windows 命名管道，拒绝远程客户端；每行一个 JSON 请求，回一行 JSON 应答。
// 请求在界面线程执行（阻塞排队连接），保证读取的状态一致。
class TestServer {
public:
    TestServer(const QString& pipeName, App* app);
    ~TestServer();

    void start();
    QString pipePath() const { return m_pipePath; }

private:
    void serve();
    QJsonObject dispatch(const QJsonObject& request);

    QString m_pipePath;
    App* m_app;
    std::thread m_thread;
    std::atomic<bool> m_stop{false};
};

} // namespace ss
