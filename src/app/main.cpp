#include "app/App.h"

#include <QApplication>
#include <QCommandLineParser>

#ifdef Q_OS_WIN
#include "platform/windows/WinPlatform.h"
#endif
#ifdef SCREENSHOT_TEST_BUILD
#include "app/testapi/TestServer.h"
#endif

int main(int argc, char* argv[])
{
    QApplication qapp(argc, argv);
    QApplication::setApplicationName(QStringLiteral("ScreenShot"));
    QApplication::setOrganizationName(QStringLiteral("ScreenShot"));
    QApplication::setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.addHelpOption();
    QCommandLineOption testPipe(QStringLiteral("test-pipe"), QStringLiteral("内置测试接口的管道名（仅测试构建）"), QStringLiteral("name"));
    parser.addOption(testPipe);
    parser.process(qapp);

    ss::Platform platform;
#ifdef Q_OS_WIN
    platform = ss::win::makeWindowsPlatform();
#endif
    ss::App app(std::move(platform));

#ifdef SCREENSHOT_TEST_BUILD
    std::unique_ptr<ss::TestServer> server;
    if (parser.isSet(testPipe)) {
        server = std::make_unique<ss::TestServer>(parser.value(testPipe), &app);
        server->start();
    }
#endif

    return qapp.exec();
}
