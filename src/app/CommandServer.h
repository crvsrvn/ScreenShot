#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace ss {

namespace win { class WinPipeServer; }

// 单实例命令通道（GEN-01、CAP-01 命令行触发）：再次启动的实例把命令行转交给已运行的实例。
// 命令是一行文本，空格分隔（双引号可包住含空格的参数）：snip | pin | pins | group | action | ocr-file | qr-file | prefs | quit
class CommandServer : public QObject {
    Q_OBJECT
public:
    explicit CommandServer(const QString& channelName, QObject* parent = nullptr);
    ~CommandServer() override;

    void start();

    // 客户端：把命令发给已运行的实例；没有实例返回 false
    static bool sendToRunningInstance(const QString& channelName, const QStringList& command, QString* reply = nullptr);
    static QString defaultChannelName();

    // 命令带 --wait 时，调用方阻塞到 reply() 被调用（最多 waitTimeoutMs）；否则立即应答 ok
    void reply(int requestId, const QString& text);

signals:
    // 在界面线程发出；requestId 用于 reply()
    void commandReceived(const QStringList& command, int requestId);

private:
    struct Pending;
    std::unique_ptr<win::WinPipeServer> m_server;
    std::mutex m_mutex;
    std::unordered_map<int, std::shared_ptr<Pending>> m_pending;
    int m_nextId = 1;
};

} // namespace ss
