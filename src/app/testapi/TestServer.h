#pragma once

#include <QJsonObject>
#include <QString>
#include <memory>

namespace ss {

class App;
namespace win { class WinPipeServer; }

// 内置测试接口（TST-03、NFR-10）：只编译进测试构建。
// 每行一个 JSON 请求，回一行 JSON 应答；请求在界面线程执行（阻塞排队连接），保证读取的状态一致。
class TestServer {
public:
    TestServer(const QString& pipeName, App* app);
    ~TestServer();

    void start();

private:
    QByteArray handleLine(const QByteArray& line);

    App* m_app;
    std::unique_ptr<win::WinPipeServer> m_server;
};

} // namespace ss
