#pragma once

#include <QJsonObject>

namespace ss {

class App;

// 在界面线程执行一条测试命令。命令清单见 TestCommands.cpp 顶部注释。
QJsonObject handleTestCommand(App& app, const QJsonObject& request);

inline bool request_is_quit(const QJsonObject& request)
{
    return request.value(QStringLiteral("cmd")).toString() == QStringLiteral("quit");
}

} // namespace ss
