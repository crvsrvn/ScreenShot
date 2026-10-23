#include "app/App.h"
#include "app/CommandServer.h"
#include "core/config/Settings.h"
#include "core/util/Log.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QLocale>
#include <QStandardPaths>
#include <QTranslator>

#if defined(Q_OS_WIN)
#include "platform/windows/WinPlatform.h"
#include "platform/windows/WinSystem.h"
#elif defined(Q_OS_MACOS)
#include "platform/macos/MacPlatform.h"
#endif
#ifdef SCREENSHOT_TEST_BUILD
#include "app/testapi/TestServer.h"
#endif

namespace {

// 界面语言（GEN-13）：翻译文件外置在程序目录的 translations/ 下
void installTranslators(QApplication& app, const QString& language)
{
    QLocale locale = language == QLatin1String("system") ? QLocale::system() : QLocale(language);
    auto* qt = new QTranslator(&app);
    if (qt->load(locale, QStringLiteral("qtbase"), QStringLiteral("_"), QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        app.installTranslator(qt);
    auto* own = new QTranslator(&app);
    const QString dir = QCoreApplication::applicationDirPath() + QStringLiteral("/translations");
    if (own->load(locale, QStringLiteral("screenshot"), QStringLiteral("_"), dir))
        app.installTranslator(own);
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication qapp(argc, argv);
    QApplication::setApplicationName(QStringLiteral("ScreenShot"));
    QApplication::setOrganizationName(QStringLiteral("ScreenShot"));
    QApplication::setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("ScreenShot - screenshot and pin tool"));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("command"), QStringLiteral("snip | pin | toggle-pins | prefs | quit (sent to the running instance)"));
    QCommandLineOption configOpt(QStringLiteral("config"), QStringLiteral("Settings file to use"), QStringLiteral("file"));
    QCommandLineOption testPipe(QStringLiteral("test-pipe"), QStringLiteral("Built-in test interface pipe name (test builds only)"), QStringLiteral("name"));
    QCommandLineOption multiInstance(QStringLiteral("allow-multiple"), QStringLiteral("Do not enforce the single instance (tests)"));
    QCommandLineOption channelOpt(QStringLiteral("channel"), QStringLiteral("Single-instance channel name (tests)"), QStringLiteral("name"));
    parser.addOptions({configOpt, testPipe, multiInstance, channelOpt});
    // 第一个非选项参数起是命令，其后的参数（含 --delay 等）原样转交，不交给解析器
    QStringList ownArgs{QCoreApplication::applicationFilePath()};
    QStringList command;
    const QStringList all = QCoreApplication::arguments();
    for (int i = 1; i < all.size(); ++i) {
        if (!command.isEmpty()) {
            command.append(all[i]);
        } else if (all[i].startsWith(QStringLiteral("--"))) {
            ownArgs.append(all[i]);
            const QString name = all[i].mid(2).section(QLatin1Char('='), 0, 0);
            if ((name == QLatin1String("config") || name == QLatin1String("test-pipe") || name == QLatin1String("channel")) && !all[i].contains(QLatin1Char('=')) && i + 1 < all.size())
                ownArgs.append(all[++i]);
        } else {
            command.append(all[i]);
        }
    }
    parser.process(ownArgs);

    // 单实例（GEN-01）：已有实例时把命令转交给它
    const QString channel = parser.isSet(channelOpt) ? parser.value(channelOpt) : ss::CommandServer::defaultChannelName();
#ifdef Q_OS_WIN
    const bool allowMultiple = parser.isSet(multiInstance);
    if (!allowMultiple && !ss::win::acquireSingleInstance(QStringLiteral("ScreenShot-") + channel)) {
        ss::CommandServer::sendToRunningInstance(channel, command.isEmpty() ? QStringList{QStringLiteral("snip")} : command);
        return 0;
    }
#endif

    // 设置与日志（CFG-02、GEN-09）
    ss::Settings settings;
    const QString configPath = ss::resolveConfigPath(parser.value(configOpt), QCoreApplication::applicationDirPath(),
                                                     QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));
    settings.load(configPath);
    settings.setFilePath(configPath);
#ifdef Q_OS_WIN
    // 以管理员身份运行（GEN-05）：未提权时用 UAC 重新启动自己
    if (settings.boolValue(QStringLiteral("general.runAsAdmin")) && !ss::win::isElevated() && !parser.isSet(testPipe) && !allowMultiple) {
        QStringList args = all.mid(1);
        if (ss::win::relaunchElevated(args))
            return 0;
    }
#endif
    const QString dataDir = QFileInfo(configPath).absolutePath();
    ss::log::Options logOptions;
    logOptions.filePath = dataDir + QStringLiteral("/logs/screenshot.log");
    logOptions.minLevel = ss::log::levelFromName(settings.stringValue(QStringLiteral("general.logLevel")));
    logOptions.maxBytes = static_cast<qint64>(settings.intValue(QStringLiteral("general.logMaxSizeKB"))) * 1024;
    ss::log::install(logOptions);
    qInfo("ScreenShot starting, config %s", qPrintable(configPath));
#ifdef Q_OS_WIN
    const QString dumpDir = dataDir + QStringLiteral("/crashes");
    const QStringList dumps = ss::win::pendingCrashDumps(dumpDir);
    ss::win::installCrashHandler(dumpDir);
#endif
    installTranslators(qapp, settings.stringValue(QStringLiteral("general.language")));

    ss::Platform platform;
#if defined(Q_OS_WIN)
    platform = ss::win::makeWindowsPlatform();
#elif defined(Q_OS_MACOS)
    platform = ss::mac::makeMacPlatform();
#endif
    ss::App app(std::move(platform), settings);
    app.setDataDir(dataDir);
    app.checkPermissions();
    app.restorePins(); // PIN-54
    app.recoverRecordings(); // VID-09
#ifdef Q_OS_WIN
    if (!allowMultiple)
        app.startCommandServer(channel);
    app.notifyCrashDumps(dumps);
#endif
    if (!command.isEmpty())
        app.runCommand(command);

#ifdef SCREENSHOT_TEST_BUILD
    std::unique_ptr<ss::TestServer> server;
    if (parser.isSet(testPipe)) {
        server = std::make_unique<ss::TestServer>(parser.value(testPipe), &app);
        server->start();
    }
#endif

    const int rc = qapp.exec();
    ss::log::uninstall();
    return rc;
}
