#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace ss {

// 动作（ACT-03）：任意功能存为动作，可绑全局热键、放进托盘菜单。
// command 与命令行接口相同（见 App::runCommand），另外两种前缀：
//   shell <命令行>   在系统 shell 里执行
//   run <程序> [参数…]  直接启动程序
struct Action {
    QString name;
    QString command;
    QString hotkey;      // 可空
    bool inTray = true;

    bool operator==(const Action&) const = default;
};

// 与设置项 actions.list（JSON 数组）互转
QList<Action> actionsFromJson(const QString& json);
QString actionsToJson(const QList<Action>& actions);

// 命令行切分：支持双引号包住含空格的参数
QStringList splitCommandLine(const QString& line);

} // namespace ss
