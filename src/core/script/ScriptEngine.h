#pragma once

#include <QString>
#include <QStringList>
#include <functional>
#include <memory>

namespace ss {

// 脚本引擎（ACT-13）：内嵌 QuickJS-ng。只开放截图 / 贴图 / 本机命令 / 剪贴板接口，不加载标准库，没有文件与网络访问。
// JS 全局对象 ss：
//   ss.command(line) → string   执行命令行接口里的命令，返回结果文本
//   ss.snip(args) / ss.pin(args) 等价于 ss.command("snip " + args) / ss.command("pin " + args)
//   ss.getClipboard() → string / ss.setClipboard(text)
//   ss.sleep(ms)
//   print(...)                   追加到输出
struct ScriptHost {
    std::function<QString(const QString& line)> command;
    std::function<QString()> getClipboard;
    std::function<void(const QString&)> setClipboard;
    std::function<void(int ms)> sleep;
};

struct ScriptResult {
    bool ok = false;
    QString output;   // print 的内容，换行分隔
    QString error;    // 异常信息
    QString value;    // 最后一个表达式的值（字符串形式）
};

class ScriptEngine {
public:
    explicit ScriptEngine(ScriptHost host);
    ~ScriptEngine();
    ScriptEngine(const ScriptEngine&) = delete;
    ScriptEngine& operator=(const ScriptEngine&) = delete;

    ScriptResult run(const QString& source, const QString& fileName = QStringLiteral("<script>"));
    // 执行超时（毫秒）：防止死循环；0 为不限制
    void setTimeout(int ms) { m_timeoutMs = ms; }

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    int m_timeoutMs = 10000;
};

} // namespace ss
