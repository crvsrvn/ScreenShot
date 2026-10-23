// 端到端测试驱动：启动测试构建的程序，经内置测试接口执行 M0 验证场景，输出报告。
// 用法：
//   screenshot_testdriver run-all --app <exe> [--report <json>] [--quick] [--only R-02]
//   screenshot_testdriver check-imports --binary <exe>     零网络导入表检查
#include "PipeClient.h"
#include "Scenarios.h"

#include "platform/windows/WinProcess.h"

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QProcess>
#include <QThread>
#include <cstdio>

using namespace ss::driver;

namespace {

int checkImports(const QString& binary)
{
    const QStringList forbidden{QStringLiteral("Qt6Network"), QStringLiteral("Qt6Qml"), QStringLiteral("Qt6Quick"), QStringLiteral("ws2_32"),
                                QStringLiteral("wininet"), QStringLiteral("winhttp"), QStringLiteral("urlmon")};
    const QStringList imports = ss::win::peImports(binary);
    if (imports.isEmpty()) {
        std::fprintf(stderr, "读取导入表失败：%s\n", qPrintable(binary));
        return 2;
    }
    int rc = 0;
    for (const auto& imp : imports) {
        for (const auto& f : forbidden) {
            if (imp.startsWith(f, Qt::CaseInsensitive)) {
                std::fprintf(stderr, "零网络检查失败：%s 导入了 %s\n", qPrintable(binary), qPrintable(imp));
                rc = 1;
            }
        }
    }
    std::fprintf(stdout, "%s 导入：%s\n", qPrintable(binary), qPrintable(imports.join(QStringLiteral(", "))));
    return rc;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addPositionalArgument(QStringLiteral("command"), QStringLiteral("run-all | check-imports"));
    QCommandLineOption appOpt(QStringLiteral("app"), QStringLiteral("被测程序"), QStringLiteral("exe"));
    QCommandLineOption reportOpt(QStringLiteral("report"), QStringLiteral("JSON 报告路径"), QStringLiteral("path"));
    QCommandLineOption quickOpt(QStringLiteral("quick"), QStringLiteral("缩短耗时项"));
    QCommandLineOption onlyOpt(QStringLiteral("only"), QStringLiteral("只运行一个场景"), QStringLiteral("id"));
    QCommandLineOption binaryOpt(QStringLiteral("binary"), QStringLiteral("要检查的 PE 文件"), QStringLiteral("path"));
    parser.addOptions({appOpt, reportOpt, quickOpt, onlyOpt, binaryOpt});
    parser.addHelpOption();
    parser.process(app);
    const QStringList args = parser.positionalArguments();
    if (args.isEmpty()) {
        parser.showHelp(1);
    }
    if (args[0] == QStringLiteral("check-imports"))
        return checkImports(parser.value(binaryOpt));
    if (args[0] != QStringLiteral("run-all")) {
        std::fprintf(stderr, "未知命令 %s\n", qPrintable(args[0]));
        return 1;
    }

    Options options;
    options.appPath = parser.value(appOpt);
    options.reportPath = parser.value(reportOpt);
    options.quick = parser.isSet(quickOpt);
    options.only = parser.value(onlyOpt);

    const QString pipeName = QStringLiteral("e2e-%1").arg(QCoreApplication::applicationPid());
    QProcess proc;
    proc.setProgram(options.appPath);
    proc.setArguments({QStringLiteral("--test-pipe"), pipeName});
    proc.setProcessChannelMode(QProcess::ForwardedChannels);
    proc.start();
    if (!proc.waitForStarted(10000)) {
        std::fprintf(stderr, "无法启动 %s\n", qPrintable(options.appPath));
        return 2;
    }
    PipeClient client;
    if (!client.connect(pipeName, 15000)) {
        std::fprintf(stderr, "连接测试接口超时\n");
        proc.kill();
        return 2;
    }
    Runner runner(options, client, static_cast<quint32>(proc.processId()));
    runner.pollNetwork();
    const QList<ScenarioResult> results = runner.runAll();
    runner.pollNetwork();
    client.call(QStringLiteral("quit"));
    client.close();
    if (!proc.waitForFinished(5000))
        proc.kill();
    const bool ok = writeReport(options.reportPath, results, runner.networkViolations());
    return ok ? 0 : 1;
}
