#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <memory>

namespace ss {

namespace win { class WinPipeServer; }

// 单实例命令通道（GEN-01、CAP-01 命令行触发）：再次启动的实例把命令行转交给已运行的实例。
// 命令是一行文本，空格分隔：snip | pin | toggle-pins | prefs | quit
class CommandServer : public QObject {
    Q_OBJECT
public:
    explicit CommandServer(const QString& channelName, QObject* parent = nullptr);
    ~CommandServer() override;

    void start();

    // 客户端：把命令发给已运行的实例；没有实例返回 false
    static bool sendToRunningInstance(const QString& channelName, const QStringList& command, QString* reply = nullptr);
    static QString defaultChannelName();

signals:
    // 在界面线程发出
    void commandReceived(const QStringList& command);

private:
    std::unique_ptr<win::WinPipeServer> m_server;
};

} // namespace ss
