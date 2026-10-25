// REQ: TST-03 TST-05 TST-06 TST-09 TST-10 NFR-01 NFR-02 NFR-03 NFR-04 NFR-05 NFR-09 NFR-10 AC-01 AC-02 AC-05 AC-08 AC-12
#include "Scenarios.h"

#include "ScrollTestWindow.h"
#include "core/util/Clock.h"
#include "platform/windows/GdiScreenCapture.h"
#include "platform/windows/WinProcess.h"
#include "platform/windows/WinScreens.h"
#include "platform/windows/WinWindowTools.h"
#include "testing/FakeScreenCapture.h"
#include "testing/ImageCompare.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <algorithm>
#include <cstdio>

#include <windows.h>
#include <wtsapi32.h>

namespace ss::driver {

namespace {

void logLine(const QString& s)
{
    std::fprintf(stdout, "%s\n", qPrintable(s));
    std::fflush(stdout);
}

double percentile(QList<double> values, double p)
{
    if (values.isEmpty())
        return 0;
    std::sort(values.begin(), values.end());
    const int idx = std::clamp(static_cast<int>(p * (values.size() - 1) + 0.5), 0, static_cast<int>(values.size() - 1));
    return values[idx];
}

// 用 SendInput 按一次按键
void pressKey(WORD vk)
{
    INPUT in[2]{};
    in[0].type = INPUT_KEYBOARD;
    in[0].ki.wVk = vk;
    in[1].type = INPUT_KEYBOARD;
    in[1].ki.wVk = vk;
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
}

// 在物理坐标上模拟鼠标拖拽
void dragMouse(QPoint from, QPoint to)
{
    auto toAbs = [](QPoint p) {
        const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN), vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
        const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN), vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        INPUT in{};
        in.type = INPUT_MOUSE;
        in.mi.dx = static_cast<LONG>((p.x() - vx) * 65535.0 / (vw - 1) + 0.5);
        in.mi.dy = static_cast<LONG>((p.y() - vy) * 65535.0 / (vh - 1) + 0.5);
        in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
        return in;
    };
    INPUT move = toAbs(from);
    SendInput(1, &move, sizeof(INPUT));
    QThread::msleep(30);
    // SendInput 的绝对坐标有取整误差，用 SetCursorPos 精确落点
    SetCursorPos(from.x(), from.y());
    QThread::msleep(30);
    INPUT down{};
    down.type = INPUT_MOUSE;
    down.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    SendInput(1, &down, sizeof(INPUT));
    QThread::msleep(30);
    const int steps = 10;
    for (int i = 1; i <= steps; ++i) {
        const QPoint p = from + (to - from) * i / steps;
        SetCursorPos(p.x(), p.y());
        QThread::msleep(15);
    }
    SetCursorPos(to.x(), to.y());
    QThread::msleep(30);
    INPUT up{};
    up.type = INPUT_MOUSE;
    up.mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(1, &up, sizeof(INPUT));
    QThread::msleep(50);
}

QString tempPng(const QString& name)
{
    return QDir::temp().filePath(QStringLiteral("screenshot-e2e-%1.png").arg(name));
}

} // namespace

Runner::Runner(Options options, PipeClient& client, quint32 appPid)
    : m_options(std::move(options))
    , m_client(client)
    , m_pid(appPid)
{
}

void Runner::pollNetwork()
{
    for (const auto& ep : win::networkEndpoints(m_pid)) {
        if (!m_networkViolations.contains(ep))
            m_networkViolations.append(ep);
    }
}

QString Runner::desktopStatus()
{
    // 会话断开（例如本进程跑在已断开的远程桌面会话里，而人坐在控制台会话前）：没有可用桌面
    WTS_CONNECTSTATE_CLASS* state = nullptr;
    DWORD bytes = 0;
    QString status;
    if (WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, WTS_CURRENT_SESSION, WTSConnectState, reinterpret_cast<LPWSTR*>(&state), &bytes) && state) {
        if (*state == WTSDisconnected)
            status = QStringLiteral("会话已断开：本进程不在当前使用的桌面会话里，请在控制台会话中运行 scripts\\e2e.cmd");
        WTSFreeMemory(state);
    }
    if (status.isEmpty())
        status = desktopUnlocked() ? QStringLiteral("可用") : QStringLiteral("锁屏中，真实输入场景将跳过");
    return status;
}

bool Runner::desktopUnlocked()
{
    // 会话锁定状态（Windows 10 的锁屏画在 Default 桌面上，只看输入桌面名会误判）
    WTSINFOEXW* info = nullptr;
    DWORD bytes = 0;
    bool locked = false;
    if (WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, WTS_CURRENT_SESSION, WTSSessionInfoEx, reinterpret_cast<LPWSTR*>(&info), &bytes) && info) {
        if (info->Level == 1)
            locked = info->Data.WTSInfoExLevel1.SessionFlags == WTS_SESSIONSTATE_LOCK;
        WTSFreeMemory(info);
    }
    if (locked)
        return false;
    HDESK desk = OpenInputDesktop(0, FALSE, GENERIC_READ);
    if (!desk)
        return false; // 输入桌面是 Winlogon（UAC、登录界面），普通进程打不开
    wchar_t name[64]{};
    DWORD len = 0;
    const bool ok = GetUserObjectInformationW(desk, UOI_NAME, name, sizeof(name), &len);
    CloseDesktop(desk);
    return ok && _wcsicmp(name, L"Default") == 0;
}

void Runner::resetApp()
{
    m_client.call(QStringLiteral("cancelCapture"));
    m_client.call(QStringLiteral("closePins"));
    m_client.call(QStringLiteral("useRealScreens"));
}

ScenarioResult Runner::skipped(const QString& id, const QString& title, const QString& reason)
{
    ScenarioResult r{id, title};
    r.skipped = true;
    r.summary = reason;
    return r;
}

QList<ScenarioResult> Runner::runAll()
{
    // R-08 紧跟 R-01：在干净的空闲状态测内存；需要真实输入的场景在锁屏时跳过
    const QStringList ids{QStringLiteral("R-01"), QStringLiteral("R-08"), QStringLiteral("R-16"), QStringLiteral("R-02"),
                          QStringLiteral("R-03"), QStringLiteral("HOTKEY"), QStringLiteral("R-06"), QStringLiteral("R-05"),
                          QStringLiteral("R-07"), QStringLiteral("R-09"), QStringLiteral("R-11"), QStringLiteral("R-17"),
                          QStringLiteral("M1-FLOW"), QStringLiteral("M1-INSTANCE"), QStringLiteral("M2-ANNOTATE"), QStringLiteral("M3-PINS"),
                          QStringLiteral("M4-SCROLL"), QStringLiteral("NFR-03"), QStringLiteral("AC-12")};
    QList<ScenarioResult> results;
    logLine(QStringLiteral("交互桌面：%1").arg(desktopStatus()));
    for (const auto& id : ids) {
        if (!m_options.only.isEmpty() && m_options.only != id)
            continue;
        logLine(QStringLiteral("==== %1").arg(id));
        resetApp();
        ScenarioResult r = run(id);
        resetApp();
        logLine(QStringLiteral("  %1：%2").arg(r.skipped ? QStringLiteral("跳过") : r.passed ? QStringLiteral("通过") : QStringLiteral("失败"), r.summary));
        results.append(r);
        pollNetwork();
    }
    return results;
}

ScenarioResult Runner::run(const QString& id)
{
    if (id == QStringLiteral("R-01")) return r01Toolchain();
    if (id == QStringLiteral("R-02")) return r02CaptureLatency();
    if (id == QStringLiteral("R-03")) return r03MixedScaleAccuracy();
    if (id == QStringLiteral("R-05")) return r05PinPerformance();
    if (id == QStringLiteral("R-06")) return r06BlurMosaic();
    if (id == QStringLiteral("R-07")) return r07LargePinScaling();
    if (id == QStringLiteral("R-08")) return r08IdleMemory();
    if (id == QStringLiteral("R-09")) return r09ElementDetectorHang();
    if (id == QStringLiteral("R-11")) return r11WindowsOcr();
    if (id == QStringLiteral("R-16")) return r16FakeMultiScreen();
    if (id == QStringLiteral("R-17")) return r17QmlNetworkDependency();
    if (id == QStringLiteral("HOTKEY")) return hotkeyEndToEnd();
    if (id == QStringLiteral("M1-FLOW")) return m1CaptureFlow();
    if (id == QStringLiteral("M1-INSTANCE")) return m1SingleInstance();
    if (id == QStringLiteral("M2-ANNOTATE")) return m2Annotate();
    if (id == QStringLiteral("M3-PINS")) return m3Pins();
    if (id == QStringLiteral("M4-SCROLL")) return m4Scroll();
    if (id == QStringLiteral("NFR-03")) return startupTime();
    if (id == QStringLiteral("AC-12")) return cliWait();
    ScenarioResult r;
    r.id = id;
    r.summary = QStringLiteral("未知场景");
    return r;
}

ScenarioResult Runner::r01Toolchain()
{
    ScenarioResult r{QStringLiteral("R-01"), QStringLiteral("工具链：Qt 6.12 + MSVC + C++20 最小程序运行")};
    const QJsonObject ping = m_client.call(QStringLiteral("ping"));
    r.passed = ping.value(QStringLiteral("ok")).toBool();
    r.data = ping;
    r.summary = r.passed ? QStringLiteral("程序已启动并响应测试接口，pid %1").arg(ping.value(QStringLiteral("pid")).toDouble())
                         : QStringLiteral("ping 失败");
    return r;
}

ScenarioResult Runner::r02CaptureLatency()
{
    ScenarioResult r{QStringLiteral("R-02"), QStringLiteral("冻结画面延迟：触发到可框选")};
    if (!desktopUnlocked())
        return skipped(r.id, r.title, QStringLiteral("锁屏中，桌面复制不可用、窗口不显示，计时不具代表性"));
    const int iterations = m_options.quick ? 100 : 1000;
    QList<double> totals, captures;
    QString backend;
    for (int i = 0; i < iterations; ++i) {
        const QJsonObject start = m_client.call(QStringLiteral("capture"));
        if (!start.value(QStringLiteral("ok")).toBool()) {
            r.summary = QStringLiteral("第 %1 次 capture 失败：%2").arg(i).arg(start.value(QStringLiteral("error")).toString());
            m_client.call(QStringLiteral("cancelCapture"));
            return r;
        }
        QJsonObject t;
        for (int wait = 0; wait < 200; ++wait) { // 最多等 2 s 首次绘制
            t = m_client.call(QStringLiteral("captureTimings"));
            if (t.value(QStringLiteral("complete")).toBool())
                break;
            QThread::msleep(10);
        }
        m_client.call(QStringLiteral("cancelCapture"));
        if (!t.value(QStringLiteral("complete")).toBool()) {
            r.summary = QStringLiteral("第 %1 次没有收到首次绘制").arg(i);
            return r;
        }
        totals.append(t.value(QStringLiteral("totalMs")).toDouble());
        captures.append(t.value(QStringLiteral("captureMs")).toDouble());
        backend = t.value(QStringLiteral("backend")).toString();
        QThread::msleep(5);
    }
    const double p50 = percentile(totals, 0.5), p95 = percentile(totals, 0.95), p99 = percentile(totals, 0.99);
    r.data = QJsonObject{{QStringLiteral("iterations"), iterations}, {QStringLiteral("p50Ms"), p50}, {QStringLiteral("p95Ms"), p95},
                         {QStringLiteral("p99Ms"), p99}, {QStringLiteral("captureP50Ms"), percentile(captures, 0.5)},
                         {QStringLiteral("captureP95Ms"), percentile(captures, 0.95)}, {QStringLiteral("backend"), backend},
                         {QStringLiteral("screens"), static_cast<int>(win::enumerateScreens().size())}};
    r.passed = p50 <= 100.0 && p95 <= 150.0;
    r.summary = QStringLiteral("%1 次，P50 %2 ms，P95 %3 ms，P99 %4 ms（其中抓屏 P50 %5 ms），后端 %6；要求 P50 ≤ 100、P95 ≤ 150")
                    .arg(iterations).arg(p50, 0, 'f', 1).arg(p95, 0, 'f', 1).arg(p99, 0, 'f', 1)
                    .arg(percentile(captures, 0.5), 0, 'f', 1).arg(backend);
    return r;
}

ScenarioResult Runner::hotkeyEndToEnd()
{
    ScenarioResult r{QStringLiteral("HOTKEY"), QStringLiteral("真实热键 F1：SendInput 到首次绘制")};
    if (!desktopUnlocked())
        return skipped(r.id, r.title, QStringLiteral("锁屏中，无法模拟按键"));
    const int iterations = m_options.quick ? 20 : 50;
    QList<double> totals;
    for (int i = 0; i < iterations; ++i) {
        const qint64 t0 = monotonicNs();
        pressKey(VK_F1);
        QJsonObject t;
        bool fresh = false;
        for (int wait = 0; wait < 200; ++wait) {
            t = m_client.call(QStringLiteral("captureTimings"));
            fresh = t.value(QStringLiteral("complete")).toBool() && t.value(QStringLiteral("triggerNs")).toDouble() >= static_cast<double>(t0);
            if (fresh)
                break;
            QThread::msleep(10);
        }
        m_client.call(QStringLiteral("cancelCapture"));
        if (!fresh) {
            r.summary = QStringLiteral("第 %1 次热键没有触发截图（热键可能被其他程序占用）").arg(i);
            return r;
        }
        totals.append(nsToMs(static_cast<qint64>(t.value(QStringLiteral("firstPaintNs")).toDouble()) - t0));
        QThread::msleep(50);
    }
    const double p50 = percentile(totals, 0.5), p95 = percentile(totals, 0.95);
    r.data = QJsonObject{{QStringLiteral("iterations"), iterations}, {QStringLiteral("p50Ms"), p50}, {QStringLiteral("p95Ms"), p95}};
    r.passed = p95 <= 150.0;
    r.summary = QStringLiteral("%1 次，按键到首次绘制 P50 %2 ms，P95 %3 ms").arg(iterations).arg(p50, 0, 'f', 1).arg(p95, 0, 'f', 1);
    return r;
}

ScenarioResult Runner::r03MixedScaleAccuracy()
{
    ScenarioResult r{QStringLiteral("R-03"), QStringLiteral("混合缩放坐标精度：真实鼠标框选 → 物理像素选区零偏差")};
    if (!desktopUnlocked())
        return skipped(r.id, r.title, QStringLiteral("锁屏中，无法模拟鼠标"));
    const auto screens = win::enumerateScreens();
    if (screens.isEmpty()) {
        r.summary = QStringLiteral("没有显示器");
        return r;
    }
    QJsonArray checks;
    bool all = true;
    // 每台显示器上各做一次框选，再做一次跨屏框选
    QList<QPair<QPoint, QPoint>> drags;
    for (const auto& s : screens) {
        const QRect g = s.physicalGeometry;
        drags.append({g.topLeft() + QPoint(g.width() / 4 + 1, g.height() / 4 + 3), g.topLeft() + QPoint(g.width() * 3 / 4 + 7, g.height() * 3 / 4 + 5)});
    }
    if (screens.size() >= 2) {
        const QRect a = screens[0].physicalGeometry, b = screens[1].physicalGeometry;
        drags.append({a.center() + QPoint(3, 1), b.center() + QPoint(-5, 2)});
    }
    for (const auto& d : drags) {
        if (!m_client.call(QStringLiteral("capture")).value(QStringLiteral("ok")).toBool()) {
            r.summary = QStringLiteral("capture 失败");
            return r;
        }
        QThread::msleep(300);
        dragMouse(d.first, d.second);
        const QJsonObject state = m_client.call(QStringLiteral("state"));
        if (state.value(QStringLiteral("selection")).toObject().value(QStringLiteral("w")).toInt() == 0) {
            // 失败截图（TST-10）
            m_client.call(QStringLiteral("screenshotDesktop"), {{QStringLiteral("path"), tempPng(QStringLiteral("r03-failure"))}});
            m_client.call(QStringLiteral("cancelCapture"));
        }
        const QJsonObject sel = state.value(QStringLiteral("selection")).toObject();
        const QRect got(sel.value(QStringLiteral("x")).toInt(), sel.value(QStringLiteral("y")).toInt(), sel.value(QStringLiteral("w")).toInt(), sel.value(QStringLiteral("h")).toInt());
        const QRect expected(QPoint(std::min(d.first.x(), d.second.x()), std::min(d.first.y(), d.second.y())),
                             QPoint(std::max(d.first.x(), d.second.x()), std::max(d.first.y(), d.second.y())));
        // 用 GDI 独立抓取同一区域与程序输出比对（画面冻结期间内容不变）
        const QString path = tempPng(QStringLiteral("r03"));
        const QJsonObject acc = got.isEmpty() ? QJsonObject{} : m_client.call(QStringLiteral("acceptCapture"), {{QStringLiteral("path"), path}});
        bool pixelsMatch = false;
        QString pixelSummary;
        if (acc.value(QStringLiteral("ok")).toBool()) {
            const QImage out(path);
            // 覆盖窗口已隐藏后再抓参照；期间桌面可能有变化（时钟等），允许少量差异像素
            QThread::msleep(100);
            const QImage ref = win::GdiScreenCapture::captureRect(expected);
            const auto cmp = testing::compareImages(out, ref, testing::CompareOptions{0, static_cast<qint64>(expected.width()) * expected.height() / 50});
            pixelsMatch = cmp.passed;
            pixelSummary = cmp.summary();
        }
        const bool okRect = got == expected;
        all = all && okRect && pixelsMatch;
        checks.append(QJsonObject{{QStringLiteral("from"), QStringLiteral("%1,%2").arg(d.first.x()).arg(d.first.y())},
                                  {QStringLiteral("to"), QStringLiteral("%1,%2").arg(d.second.x()).arg(d.second.y())},
                                  {QStringLiteral("expected"), QStringLiteral("%1,%2 %3x%4").arg(expected.x()).arg(expected.y()).arg(expected.width()).arg(expected.height())},
                                  {QStringLiteral("got"), QStringLiteral("%1,%2 %3x%4").arg(got.x()).arg(got.y()).arg(got.width()).arg(got.height())},
                                  {QStringLiteral("rectOk"), okRect}, {QStringLiteral("pixelsOk"), pixelsMatch}, {QStringLiteral("pixels"), pixelSummary}});
        logLine(QStringLiteral("  拖拽 %1 → 选区 %2 %3").arg(checks.last().toObject().value(QStringLiteral("expected")).toString(), checks.last().toObject().value(QStringLiteral("got")).toString(), okRect ? QStringLiteral("一致") : QStringLiteral("不一致")));
    }
    r.data = QJsonObject{{QStringLiteral("checks"), checks}, {QStringLiteral("screens"), static_cast<int>(screens.size())}};
    r.passed = all;
    r.summary = QStringLiteral("%1 次框选，%2").arg(drags.size()).arg(all ? QStringLiteral("选区与输出像素全部一致") : QStringLiteral("有偏差，见报告"));
    return r;
}

ScenarioResult Runner::r16FakeMultiScreen()
{
    ScenarioResult r{QStringLiteral("R-16"), QStringLiteral("测试替身：合成两台 4K（150% / 100%）跨屏裁剪逐像素一致")};
    QJsonArray screens{QJsonObject{{QStringLiteral("id"), QStringLiteral("FAKE-A")}, {QStringLiteral("x"), 0}, {QStringLiteral("y"), 0}, {QStringLiteral("w"), 3840}, {QStringLiteral("h"), 2160}, {QStringLiteral("scale"), 1.5}},
                        QJsonObject{{QStringLiteral("id"), QStringLiteral("FAKE-B")}, {QStringLiteral("x"), 3840}, {QStringLiteral("y"), 0}, {QStringLiteral("w"), 3840}, {QStringLiteral("h"), 2160}, {QStringLiteral("scale"), 1.0}}};
    if (!m_client.call(QStringLiteral("useFakeScreens"), {{QStringLiteral("screens"), screens}}).value(QStringLiteral("ok")).toBool()) {
        r.summary = QStringLiteral("useFakeScreens 失败");
        return r;
    }
    const QRect region(3700, 1000, 300, 200);
    bool ok = m_client.call(QStringLiteral("capture"), {{QStringLiteral("show"), false}}).value(QStringLiteral("ok")).toBool();
    ok = ok && m_client.call(QStringLiteral("setSelection"), {{QStringLiteral("x"), region.x()}, {QStringLiteral("y"), region.y()}, {QStringLiteral("w"), region.width()}, {QStringLiteral("h"), region.height()}}).value(QStringLiteral("ok")).toBool();
    const QString path = tempPng(QStringLiteral("r16"));
    ok = ok && m_client.call(QStringLiteral("acceptCapture"), {{QStringLiteral("path"), path}}).value(QStringLiteral("ok")).toBool();
    m_client.call(QStringLiteral("useRealScreens"));
    if (!ok) {
        r.summary = QStringLiteral("合成截图流程失败");
        return r;
    }
    const QImage out(path);
    QImage expected(region.size(), QImage::Format_ARGB32);
    for (int y = 0; y < region.height(); ++y)
        for (int x = 0; x < region.width(); ++x) {
            const int px = region.x() + x, py = region.y() + y;
            expected.setPixel(x, y, px < 3840 ? testing::FakeScreenCapture::patternPixel(0, px, py) : testing::FakeScreenCapture::patternPixel(1, px - 3840, py));
        }
    const auto cmp = testing::compareImages(out, expected);
    r.passed = cmp.passed;
    r.summary = cmp.summary();
    return r;
}

ScenarioResult Runner::r05PinPerformance()
{
    ScenarioResult r{QStringLiteral("R-05"), QStringLiteral("CPU 透明贴图：50 个 1080p 贴图逐个拖动、缩放")};
    const int count = m_options.quick ? 50 : 50;
    const QJsonObject created = m_client.call(QStringLiteral("createPins"), {{QStringLiteral("count"), count}, {QStringLiteral("width"), 1920}, {QStringLiteral("height"), 1080}});
    if (!created.value(QStringLiteral("ok")).toBool()) {
        r.summary = QStringLiteral("创建贴图失败");
        return r;
    }
    QThread::msleep(500);
    QList<double> moveRates, scaleFps, scaleMax;
    const int sample = m_options.quick ? 10 : count;
    for (int i = 0; i < sample; ++i) {
        const int idx = i * count / sample;
        const QJsonObject d = m_client.call(QStringLiteral("dragPin"), {{QStringLiteral("index"), idx}, {QStringLiteral("dx"), 3}, {QStringLiteral("dy"), 2}, {QStringLiteral("steps"), 120}});
        moveRates.append(d.value(QStringLiteral("movesPerSecond")).toDouble());
        const QJsonObject s = m_client.call(QStringLiteral("scalePin"), {{QStringLiteral("index"), idx}, {QStringLiteral("steps"), 30}, {QStringLiteral("factor"), 0.97}});
        scaleFps.append(s.value(QStringLiteral("fps")).toDouble());
        scaleMax.append(s.value(QStringLiteral("maxFrameMs")).toDouble());
    }
    const QJsonObject mem = m_client.call(QStringLiteral("memory"));
    m_client.call(QStringLiteral("closePins"));
    const double minMoveRate = *std::min_element(moveRates.begin(), moveRates.end());
    const double minScaleFps = *std::min_element(scaleFps.begin(), scaleFps.end());
    const double worstFrame = *std::max_element(scaleMax.begin(), scaleMax.end());
    r.data = QJsonObject{{QStringLiteral("pins"), count}, {QStringLiteral("minMovesPerSecond"), minMoveRate}, {QStringLiteral("minScaleFps"), minScaleFps},
                         {QStringLiteral("worstScaleFrameMs"), worstFrame}, {QStringLiteral("privateBytesWithPins"), mem.value(QStringLiteral("privateBytes")).toDouble()}};
    r.passed = minMoveRate >= 60.0 && minScaleFps >= 60.0;
    r.summary = QStringLiteral("拖动最低 %1 次/秒，缩放最低 %2 FPS（最慢一帧 %3 ms），带贴图私有内存 %4 MB；要求 ≥ 60")
                    .arg(minMoveRate, 0, 'f', 0).arg(minScaleFps, 0, 'f', 0).arg(worstFrame, 0, 'f', 1)
                    .arg(mem.value(QStringLiteral("privateBytes")).toDouble() / 1048576.0, 0, 'f', 0);
    return r;
}

ScenarioResult Runner::r06BlurMosaic()
{
    ScenarioResult r{QStringLiteral("R-06"), QStringLiteral("CPU 4K 高斯模糊与马赛克")};
    const QJsonObject blur = m_client.call(QStringLiteral("benchBlur"), {{QStringLiteral("radius"), 10}});
    const QJsonObject blurBig = m_client.call(QStringLiteral("benchBlur"), {{QStringLiteral("radius"), 40}});
    const QJsonObject mosaic = m_client.call(QStringLiteral("benchMosaic"), {{QStringLiteral("block"), 12}});
    const double b = blur.value(QStringLiteral("avgMs")).toDouble(), bb = blurBig.value(QStringLiteral("avgMs")).toDouble(), m = mosaic.value(QStringLiteral("avgMs")).toDouble();
    r.data = QJsonObject{{QStringLiteral("blurR10AvgMs"), b}, {QStringLiteral("blurR40AvgMs"), bb}, {QStringLiteral("mosaicAvgMs"), m}};
    r.passed = b <= 100.0 && bb <= 100.0 && m <= 100.0;
    r.summary = QStringLiteral("模糊半径 10：%1 ms，半径 40：%2 ms，马赛克：%3 ms；要求 ≤ 100").arg(b, 0, 'f', 1).arg(bb, 0, 'f', 1).arg(m, 0, 'f', 1);
    return r;
}

ScenarioResult Runner::r07LargePinScaling()
{
    ScenarioResult r{QStringLiteral("R-07"), QStringLiteral("CPU 大图缩放：8K 贴图连续缩放")};
    if (!m_client.call(QStringLiteral("createPins"), {{QStringLiteral("count"), 1}, {QStringLiteral("width"), 7680}, {QStringLiteral("height"), 4320}}).value(QStringLiteral("ok")).toBool()) {
        r.summary = QStringLiteral("创建 8K 贴图失败");
        return r;
    }
    QThread::msleep(300);
    // 新贴图已自动缩小到屏幕内（PIN-42）；先缩小再放大到超出屏幕（窗口只覆盖可见部分）
    const QJsonObject down = m_client.call(QStringLiteral("scalePin"), {{QStringLiteral("index"), 0}, {QStringLiteral("steps"), 40}, {QStringLiteral("factor"), 0.95}});
    const QJsonObject up = m_client.call(QStringLiteral("scalePin"), {{QStringLiteral("index"), 0}, {QStringLiteral("steps"), 60}, {QStringLiteral("factor"), 1.06}});
    m_client.call(QStringLiteral("closePins"));
    const double fpsDown = down.value(QStringLiteral("fps")).toDouble(), fpsUp = up.value(QStringLiteral("fps")).toDouble();
    const double worst = std::max(down.value(QStringLiteral("maxFrameMs")).toDouble(), up.value(QStringLiteral("maxFrameMs")).toDouble());
    r.data = QJsonObject{{QStringLiteral("fpsDown"), fpsDown}, {QStringLiteral("fpsUp"), fpsUp}, {QStringLiteral("worstFrameMs"), worst},
                         {QStringLiteral("finalScale"), up.value(QStringLiteral("scale")).toDouble()}, {QStringLiteral("finalClipped"), up.value(QStringLiteral("clipped")).toBool()}};
    r.passed = fpsDown >= 30.0 && fpsUp >= 30.0;
    r.summary = QStringLiteral("缩小 %1 FPS，放大 %2 FPS（最终 %4 倍，超出屏幕=%5），最慢一帧 %3 ms；要求 ≥ 30")
                    .arg(fpsDown, 0, 'f', 0).arg(fpsUp, 0, 'f', 0).arg(worst, 0, 'f', 1)
                    .arg(up.value(QStringLiteral("scale")).toDouble(), 0, 'f', 2).arg(up.value(QStringLiteral("clipped")).toBool() ? QStringLiteral("是") : QStringLiteral("否"));
    return r;
}

ScenarioResult Runner::r08IdleMemory()
{
    ScenarioResult r{QStringLiteral("R-08"), QStringLiteral("常驻内存：无贴图空闲后的私有内存")};
    const int idleSeconds = m_options.quick ? 10 : 600;
    QThread::sleep(static_cast<unsigned long>(idleSeconds));
    const QJsonObject mem = m_client.call(QStringLiteral("memory"));
    const double mb = mem.value(QStringLiteral("privateBytes")).toDouble() / 1048576.0;
    r.data = QJsonObject{{QStringLiteral("idleSeconds"), idleSeconds}, {QStringLiteral("privateMB"), mb}};
    r.passed = mb <= 150.0;
    r.summary = QStringLiteral("空闲 %1 s 后私有内存 %2 MB；要求 ≤ 150 MB（Snipaste 基线需同条件另测）").arg(idleSeconds).arg(mb, 0, 'f', 1);
    return r;
}

ScenarioResult Runner::r09ElementDetectorHang()
{
    ScenarioResult r{QStringLiteral("R-09"), QStringLiteral("UI 元素检测：挂起的程序不卡界面，超时后降级")};
    if (!desktopUnlocked())
        return skipped(r.id, r.title, QStringLiteral("锁屏中，任务栏与测试窗口都不在输入桌面上"));
    // 1) 正常窗口：任务栏
    const auto screens = win::enumerateScreens();
    QPoint taskbar;
    for (const auto& s : screens)
        if (s.primary)
            taskbar = QPoint(s.physicalGeometry.center().x(), s.physicalGeometry.bottom() - 10);
    const QJsonObject normal = m_client.call(QStringLiteral("elementAt"), {{QStringLiteral("x"), taskbar.x()}, {QStringLiteral("y"), taskbar.y()}, {QStringLiteral("timeoutMs"), 500}});
    // 2) 挂起窗口：本进程创建一个窗口后让它的线程永不处理消息
    struct Hung {
        HWND hwnd = nullptr;
        HANDLE ready = nullptr;
    } hung;
    hung.ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE thread = CreateThread(nullptr, 0, [](LPVOID p) -> DWORD {
        auto* h = static_cast<Hung*>(p);
        WNDCLASSW wc{};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"ScreenShotHungWindow";
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        RegisterClassW(&wc);
        h->hwnd = CreateWindowExW(WS_EX_TOPMOST, wc.lpszClassName, L"Hung test window", WS_POPUP | WS_VISIBLE, 100, 100, 400, 300, nullptr, nullptr, wc.hInstance, nullptr);
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        SetEvent(h->ready);
        Sleep(INFINITE); // 永远不再处理消息
        return 0;
    }, &hung, 0, nullptr);
    WaitForSingleObject(hung.ready, 5000);
    QThread::msleep(300);
    QElapsedTimer t;
    t.start();
    const QJsonObject hungResult = m_client.call(QStringLiteral("elementAt"), {{QStringLiteral("x"), 200}, {QStringLiteral("y"), 200}, {QStringLiteral("timeoutMs"), 300}});
    const double roundTrip = t.elapsed();
    // 挂起期间再发一条，必须立即被拒绝而不是排队
    t.restart();
    const QJsonObject during = m_client.call(QStringLiteral("elementAt"), {{QStringLiteral("x"), 200}, {QStringLiteral("y"), 200}, {QStringLiteral("timeoutMs"), 300}});
    const double duringMs = t.elapsed();
    if (hung.hwnd)
        DestroyWindow(hung.hwnd); // 跨线程销毁会失败，但进程退出时自动清理
    TerminateThread(thread, 0);
    CloseHandle(thread);
    CloseHandle(hung.ready);
    const double hungElapsed = hungResult.value(QStringLiteral("elapsedMs")).toDouble();
    r.data = QJsonObject{{QStringLiteral("normalFound"), normal.value(QStringLiteral("found")).toBool()}, {QStringLiteral("normalMs"), normal.value(QStringLiteral("elapsedMs")).toDouble()},
                         {QStringLiteral("hungFound"), hungResult.value(QStringLiteral("found")).toBool()}, {QStringLiteral("hungMs"), hungElapsed},
                         {QStringLiteral("hungRoundTripMs"), roundTrip}, {QStringLiteral("secondCallMs"), duringMs}};
    // 通过标准：挂起窗口的检测在超时 + 100 ms 内返回，界面线程往返同样不超过 500 ms
    r.passed = hungElapsed <= 400.0 && roundTrip <= 600.0 && duringMs <= 600.0;
    r.summary = QStringLiteral("任务栏元素%1（%2 ms）；挂起窗口 %3 ms 返回（往返 %4 ms），挂起期间第二次调用 %5 ms")
                    .arg(normal.value(QStringLiteral("found")).toBool() ? QStringLiteral("找到") : QStringLiteral("未找到"))
                    .arg(normal.value(QStringLiteral("elapsedMs")).toDouble(), 0, 'f', 0).arg(hungElapsed, 0, 'f', 0).arg(roundTrip, 0, 'f', 0).arg(duringMs, 0, 'f', 0);
    return r;
}

ScenarioResult Runner::r11WindowsOcr()
{
    ScenarioResult r{QStringLiteral("R-11"), QStringLiteral("Windows 系统 OCR（未打包程序经 C++/WinRT）")};
    const QJsonObject o = m_client.call(QStringLiteral("ocrSelfTest"));
    const QString text = o.value(QStringLiteral("text")).toString();
    const bool english = text.contains(QStringLiteral("ScreenShot"), Qt::CaseInsensitive) || text.contains(QStringLiteral("12345"));
    const bool chinese = text.contains(QStringLiteral("截图")) || text.contains(QStringLiteral("识别")) || text.contains(QStringLiteral("测试"));
    r.data = o;
    r.passed = o.value(QStringLiteral("ok")).toBool() && english;
    r.summary = QStringLiteral("可用=%1，英文%2，中文%3，耗时 %4 ms，识别结果：%5 %6")
                    .arg(o.value(QStringLiteral("available")).toBool() ? QStringLiteral("是") : QStringLiteral("否"))
                    .arg(english ? QStringLiteral("通过") : QStringLiteral("失败")).arg(chinese ? QStringLiteral("通过") : QStringLiteral("失败（系统可能没有中文 OCR 包）"))
                    .arg(o.value(QStringLiteral("elapsedMs")).toDouble(), 0, 'f', 0).arg(text.simplified().left(80), o.value(QStringLiteral("error")).toString());
    return r;
}

ScenarioResult Runner::r17QmlNetworkDependency()
{
    ScenarioResult r{QStringLiteral("R-17"), QStringLiteral("Qt Qml 是否依赖 Qt Network")};
    const QString qtBin = QString::fromLocal8Bit(qgetenv("PATH")).split(QLatin1Char(';')).filter(QStringLiteral("Qt")).value(0);
    QString qml = QDir(qtBin).filePath(QStringLiteral("Qt6Qml.dll"));
    if (!QFile::exists(qml))
        qml = QStringLiteral("C:/Qt/6.12.0/msvc2022_64/bin/Qt6Qml.dll");
    if (!QFile::exists(qml)) {
        r.skipped = true;
        r.summary = QStringLiteral("找不到 Qt6Qml.dll");
        return r;
    }
    const QStringList imports = win::peImports(qml);
    const bool dependsOnNetwork = imports.filter(QStringLiteral("Qt6Network"), Qt::CaseInsensitive).size() > 0;
    r.data = QJsonObject{{QStringLiteral("dll"), qml}, {QStringLiteral("imports"), QJsonArray::fromStringList(imports)}, {QStringLiteral("dependsOnQtNetwork"), dependsOnNetwork}};
    r.passed = true; // 只记录结论，决定脚本引擎方案
    r.summary = dependsOnNetwork ? QStringLiteral("Qt6Qml.dll 导入了 Qt6Network.dll → 维持 QuickJS-ng 方案")
                                 : QStringLiteral("Qt6Qml.dll 不直接导入 Qt6Network.dll（导入：%1）").arg(imports.join(QStringLiteral(", ")));
    return r;
}

ScenarioResult Runner::m1CaptureFlow()
{
    // REQ: CAP-01 CAP-10 CAP-40 OUT-08 GEN-09
    ScenarioResult r{QStringLiteral("M1-FLOW"), QStringLiteral("M1：真实桌面上框选、快速保存、历史")};
    QTemporaryDir dir;
    m_client.call(QStringLiteral("setting"), {{QStringLiteral("key"), QStringLiteral("output.quickSaveDir")}, {QStringLiteral("value"), dir.path()}});
    m_client.call(QStringLiteral("setting"), {{QStringLiteral("key"), QStringLiteral("output.fileNamePattern")}, {QStringLiteral("value"), QStringLiteral("e2e_{n}")}});
    if (!m_client.call(QStringLiteral("capture")).value(QStringLiteral("ok")).toBool()) {
        r.summary = QStringLiteral("capture 失败");
        return r;
    }
    const auto screens = win::enumerateScreens();
    const QRect g = screens.first().physicalGeometry;
    const QPoint a = g.topLeft() + QPoint(200, 200), b = g.topLeft() + QPoint(519, 439);
    m_client.call(QStringLiteral("mouse"), {{QStringLiteral("action"), QStringLiteral("press")}, {QStringLiteral("x"), a.x()}, {QStringLiteral("y"), a.y()}});
    m_client.call(QStringLiteral("mouse"), {{QStringLiteral("action"), QStringLiteral("move")}, {QStringLiteral("x"), a.x() + 50}, {QStringLiteral("y"), a.y() + 50}});
    m_client.call(QStringLiteral("mouse"), {{QStringLiteral("action"), QStringLiteral("release")}, {QStringLiteral("x"), b.x()}, {QStringLiteral("y"), b.y()}});
    const QJsonObject st = m_client.call(QStringLiteral("state"));
    const QJsonObject sel = st.value(QStringLiteral("selection")).toObject();
    const QRect got(sel.value(QStringLiteral("x")).toInt(), sel.value(QStringLiteral("y")).toInt(), sel.value(QStringLiteral("w")).toInt(), sel.value(QStringLiteral("h")).toInt());
    const bool selOk = got == QRect(a, b) && st.value(QStringLiteral("state")).toString() == QLatin1String("selected") && st.value(QStringLiteral("toolbarVisible")).toBool();
    // 锚点拖动：右下角再拉 10 像素
    m_client.call(QStringLiteral("mouse"), {{QStringLiteral("action"), QStringLiteral("press")}, {QStringLiteral("x"), b.x()}, {QStringLiteral("y"), b.y()}});
    m_client.call(QStringLiteral("mouse"), {{QStringLiteral("action"), QStringLiteral("move")}, {QStringLiteral("x"), b.x() + 10}, {QStringLiteral("y"), b.y() + 10}});
    m_client.call(QStringLiteral("mouse"), {{QStringLiteral("action"), QStringLiteral("release")}, {QStringLiteral("x"), b.x() + 10}, {QStringLiteral("y"), b.y() + 10}});
    const QJsonObject sel2 = m_client.call(QStringLiteral("state")).value(QStringLiteral("selection")).toObject();
    const bool resizeOk = sel2.value(QStringLiteral("w")).toInt() == 330 && sel2.value(QStringLiteral("h")).toInt() == 250;
    // 当前图像与 GDI 参照比对，然后快速保存
    const QString cur = tempPng(QStringLiteral("m1-current"));
    m_client.call(QStringLiteral("currentImage"), {{QStringLiteral("path"), cur}});
    const QImage out(cur);
    const QImage ref = win::GdiScreenCapture::captureRect(QRect(a, QSize(330, 250)));
    const auto cmp = testing::compareImages(out, ref, testing::CompareOptions{0, static_cast<qint64>(330) * 250 / 50});
    m_client.call(QStringLiteral("key"), {{QStringLiteral("key"), QStringLiteral("Ctrl+Shift+S")}});
    const QJsonObject after = m_client.call(QStringLiteral("state"));
    const QStringList savedFiles = QDir(dir.path()).entryList({QStringLiteral("e2e_*.png")}, QDir::Files, QDir::Time);
    const QString saved = savedFiles.isEmpty() ? QString() : QDir(dir.path()).filePath(savedFiles.first());
    const bool savedOk = QFile::exists(saved) && QImage(saved).size() == QSize(330, 250) && !after.value(QStringLiteral("capturing")).toBool()
        && after.value(QStringLiteral("historySize")).toInt() >= 1;
    // 历史回放
    m_client.call(QStringLiteral("capture"));
    m_client.call(QStringLiteral("key"), {{QStringLiteral("key"), QStringLiteral(",")}});
    const QJsonObject hist = m_client.call(QStringLiteral("state"));
    const bool histOk = hist.value(QStringLiteral("state")).toString() == QLatin1String("history") && hist.value(QStringLiteral("selection")).toObject().value(QStringLiteral("w")).toInt() == 330;
    m_client.call(QStringLiteral("cancelCapture"));
    r.data = QJsonObject{{QStringLiteral("selectionOk"), selOk}, {QStringLiteral("resizeOk"), resizeOk}, {QStringLiteral("pixels"), cmp.summary()}, {QStringLiteral("savedOk"), savedOk}, {QStringLiteral("historyOk"), histOk}};
    r.passed = selOk && resizeOk && cmp.passed && savedOk && histOk;
    r.summary = QStringLiteral("框选%1，锚点%2，像素%3，快速保存%4，历史%5").arg(selOk ? QStringLiteral("ok") : QStringLiteral("FAIL"), resizeOk ? QStringLiteral("ok") : QStringLiteral("FAIL"),
                                                                 cmp.passed ? QStringLiteral("ok") : cmp.summary(), savedOk ? QStringLiteral("ok") : QStringLiteral("FAIL"), histOk ? QStringLiteral("ok") : QStringLiteral("FAIL"));
    return r;
}

ScenarioResult Runner::m1SingleInstance()
{
    // REQ: GEN-01 CAP-01
    ScenarioResult r{QStringLiteral("M1-INSTANCE"), QStringLiteral("M1：第二个实例把 snip 命令转交给运行中的实例")};
    QProcess second;
    second.setProgram(m_options.appPath);
    second.setArguments({QStringLiteral("--channel"), m_options.channel, QStringLiteral("snip")});
    second.start();
    const bool exited = second.waitForFinished(10000);
    QThread::msleep(300);
    const QJsonObject st = m_client.call(QStringLiteral("state"));
    const bool capturing = st.value(QStringLiteral("capturing")).toBool();
    m_client.call(QStringLiteral("cancelCapture"));
    r.data = QJsonObject{{QStringLiteral("secondExited"), exited}, {QStringLiteral("exitCode"), second.exitCode()}, {QStringLiteral("capturing"), capturing}};
    r.passed = exited && second.exitCode() == 0 && capturing;
    r.summary = QStringLiteral("第二实例退出码 %1，运行中的实例%2开始截图").arg(second.exitCode()).arg(capturing ? QStringLiteral("已") : QStringLiteral("未"));
    return r;
}

ScenarioResult Runner::m2Annotate()
{
    // REQ: ANN-01 ANN-20 NFR-08
    ScenarioResult r{QStringLiteral("M2-ANNOTATE"), QStringLiteral("M2：真实桌面上框选、画填充矩形、快速保存后像素正确")};
    QTemporaryDir dir;
    m_client.call(QStringLiteral("setting"), {{QStringLiteral("key"), QStringLiteral("output.quickSaveDir")}, {QStringLiteral("value"), dir.path()}});
    m_client.call(QStringLiteral("setting"), {{QStringLiteral("key"), QStringLiteral("output.fileNamePattern")}, {QStringLiteral("value"), QStringLiteral("ann_{n}")}});
    if (!m_client.call(QStringLiteral("capture")).value(QStringLiteral("ok")).toBool()) {
        r.summary = QStringLiteral("capture 失败");
        return r;
    }
    const QRect g = win::enumerateScreens().first().physicalGeometry;
    const QPoint a = g.topLeft() + QPoint(300, 300);
    m_client.call(QStringLiteral("setSelection"), {{QStringLiteral("x"), a.x()}, {QStringLiteral("y"), a.y()}, {QStringLiteral("w"), 200}, {QStringLiteral("h"), 150}});
    const bool toolbarShown = m_client.call(QStringLiteral("state")).value(QStringLiteral("toolbarVisible")).toBool();
    m_client.call(QStringLiteral("annotate"), {{QStringLiteral("tool"), QStringLiteral("rect")}});
    m_client.call(QStringLiteral("annotateStyle"), {{QStringLiteral("color"), QStringLiteral("#ff00ff00")}, {QStringLiteral("filled"), true}});
    m_client.call(QStringLiteral("mouse"), {{QStringLiteral("action"), QStringLiteral("press")}, {QStringLiteral("x"), a.x() + 20}, {QStringLiteral("y"), a.y() + 20}});
    m_client.call(QStringLiteral("mouse"), {{QStringLiteral("action"), QStringLiteral("move")}, {QStringLiteral("x"), a.x() + 60}, {QStringLiteral("y"), a.y() + 60}});
    m_client.call(QStringLiteral("mouse"), {{QStringLiteral("action"), QStringLiteral("release")}, {QStringLiteral("x"), a.x() + 119}, {QStringLiteral("y"), a.y() + 99}});
    const QJsonObject st = m_client.call(QStringLiteral("annotateState"));
    const bool oneItem = st.value(QStringLiteral("count")).toInt() == 1 && m_client.call(QStringLiteral("state")).value(QStringLiteral("state")).toString() == QLatin1String("annotating");
    m_client.call(QStringLiteral("key"), {{QStringLiteral("key"), QStringLiteral("Ctrl+Shift+S")}});
    const QStringList files = QDir(dir.path()).entryList({QStringLiteral("ann_*.png")}, QDir::Files, QDir::Time);
    const QString saved = files.isEmpty() ? QString() : QDir(dir.path()).filePath(files.first());
    const QImage img(saved);
    const bool pixelsOk = !img.isNull() && img.size() == QSize(200, 150) && img.pixel(60, 60) == qRgb(0, 255, 0) && img.pixel(5, 5) != qRgb(0, 255, 0);
    r.data = QJsonObject{{QStringLiteral("toolbarShown"), toolbarShown}, {QStringLiteral("oneItem"), oneItem}, {QStringLiteral("pixelsOk"), pixelsOk}, {QStringLiteral("file"), saved}};
    r.passed = toolbarShown && oneItem && pixelsOk;
    r.summary = QStringLiteral("工具栏%1，标注%2，保存像素%3").arg(toolbarShown ? QStringLiteral("ok") : QStringLiteral("FAIL"), oneItem ? QStringLiteral("ok") : QStringLiteral("FAIL"), pixelsOk ? QStringLiteral("ok") : QStringLiteral("FAIL"));
    return r;
}

ScenarioResult Runner::m3Pins()
{
    // REQ: PIN-02 PIN-04 PIN-22 PIN-33 PIN-50 AC-06 AC-07
    ScenarioResult r{QStringLiteral("M3-PINS"), QStringLiteral("M3：贴图在真实桌面上的物理定位、缩放、穿透、关闭恢复、文本贴图")};
    const QRect g = win::enumerateScreens().first().physicalGeometry;
    const QPoint at = g.topLeft() + QPoint(400, 300);
    bool clipboardOk = true;
    if (desktopUnlocked()) {
        clipboardOk = m_client.call(QStringLiteral("pinAction"), {{QStringLiteral("action"), QStringLiteral("clipboardImage")}, {QStringLiteral("x"), at.x()}, {QStringLiteral("y"), at.y()}, {QStringLiteral("width"), 320}, {QStringLiteral("height"), 200}}).value(QStringLiteral("ok")).toBool();
    } else {
        // 锁屏时剪贴板不可用：直接创建贴图再移到目标位置
        m_client.call(QStringLiteral("createPins"), {{QStringLiteral("count"), 1}, {QStringLiteral("width"), 320}, {QStringLiteral("height"), 200}});
        m_client.call(QStringLiteral("pinAction"), {{QStringLiteral("index"), 0}, {QStringLiteral("action"), QStringLiteral("move")}, {QStringLiteral("x"), at.x()}, {QStringLiteral("y"), at.y()}});
    }
    QThread::msleep(200);
    if (m_client.call(QStringLiteral("pins")).value(QStringLiteral("pins")).toArray().isEmpty()) {
        r.summary = QStringLiteral("没有创建出贴图");
        return r;
    }
    QJsonObject pins = m_client.call(QStringLiteral("pins"));
    QJsonObject p = pins.value(QStringLiteral("pins")).toArray().first().toObject();
    const QRect content(p.value(QStringLiteral("x")).toInt(), p.value(QStringLiteral("y")).toInt(), p.value(QStringLiteral("w")).toInt(), p.value(QStringLiteral("h")).toInt());
    const QJsonObject nat = p.value(QStringLiteral("native")).toObject();
    const QRect native(nat.value(QStringLiteral("x")).toInt(), nat.value(QStringLiteral("y")).toInt(), nat.value(QStringLiteral("w")).toInt(), nat.value(QStringLiteral("h")).toInt());
    // PIN-02：内容矩形与请求位置重合；原生窗口 = 内容 + 阴影边距（24 逻辑像素 × 缩放）
    const bool placed = content.topLeft() == at && content.size() == QSize(320, 200);
    const bool nativeOk = native.contains(content) && native.width() - content.width() <= 2 * 24 * 2 + 2;
    // 缩放两步后尺寸按步长变化
    m_client.call(QStringLiteral("pinAction"), {{QStringLiteral("index"), 0}, {QStringLiteral("action"), QStringLiteral("zoom")}, {QStringLiteral("steps"), 2}});
    p = m_client.call(QStringLiteral("pins")).value(QStringLiteral("pins")).toArray().first().toObject();
    const bool zoomed = std::abs(p.value(QStringLiteral("scale")).toDouble() - 1.21) < 0.001 && p.value(QStringLiteral("w")).toInt() == qRound(320 * 1.21);
    // 穿透
    m_client.call(QStringLiteral("pinAction"), {{QStringLiteral("action"), QStringLiteral("passthroughAt")}, {QStringLiteral("x"), at.x() + 10}, {QStringLiteral("y"), at.y() + 10}});
    const bool through = m_client.call(QStringLiteral("pins")).value(QStringLiteral("pins")).toArray().first().toObject().value(QStringLiteral("passthrough")).toBool();
    m_client.call(QStringLiteral("pinAction"), {{QStringLiteral("action"), QStringLiteral("passthroughAt")}, {QStringLiteral("x"), g.right() - 5}, {QStringLiteral("y"), g.bottom() - 5}});
    const bool throughOff = !m_client.call(QStringLiteral("pins")).value(QStringLiteral("pins")).toArray().first().toObject().value(QStringLiteral("passthrough")).toBool();
    // 关闭 → 恢复
    m_client.call(QStringLiteral("pinAction"), {{QStringLiteral("index"), 0}, {QStringLiteral("action"), QStringLiteral("close")}});
    QThread::msleep(100);
    const int closed = m_client.call(QStringLiteral("pins")).value(QStringLiteral("closed")).toInt();
    m_client.call(QStringLiteral("pinAction"), {{QStringLiteral("action"), QStringLiteral("restore")}});
    QThread::msleep(100);
    pins = m_client.call(QStringLiteral("pins"));
    const bool restored = closed == 1 && pins.value(QStringLiteral("pins")).toArray().size() == 1 && std::abs(pins.value(QStringLiteral("pins")).toArray().first().toObject().value(QStringLiteral("scale")).toDouble() - 1.21) < 0.001;
    // 文本贴图
    const QJsonObject text = m_client.call(QStringLiteral("pinAction"), {{QStringLiteral("action"), QStringLiteral("text")}, {QStringLiteral("text"), QStringLiteral("贴图测试 pin text")}, {QStringLiteral("x"), at.x()}, {QStringLiteral("y"), at.y() + 300}});
    const bool textOk = text.value(QStringLiteral("ok")).toBool() && text.value(QStringLiteral("w")).toInt() > 50;
    m_client.call(QStringLiteral("closePins"));
    r.data = QJsonObject{{QStringLiteral("content"), QStringLiteral("%1,%2 %3x%4").arg(content.x()).arg(content.y()).arg(content.width()).arg(content.height())},
                         {QStringLiteral("native"), QStringLiteral("%1,%2 %3x%4").arg(native.x()).arg(native.y()).arg(native.width()).arg(native.height())},
                         {QStringLiteral("placed"), placed}, {QStringLiteral("nativeOk"), nativeOk}, {QStringLiteral("zoomed"), zoomed}, {QStringLiteral("through"), through},
                         {QStringLiteral("throughOff"), throughOff}, {QStringLiteral("restored"), restored}, {QStringLiteral("textOk"), textOk}};
    r.passed = clipboardOk && placed && nativeOk && zoomed && through && throughOff && restored && textOk;
    r.summary = QStringLiteral("剪贴板%8 定位%1 原生%2 缩放%3 穿透%4/%5 恢复%6 文本%7")
                    .arg(placed ? QStringLiteral("ok") : QStringLiteral("FAIL"), nativeOk ? QStringLiteral("ok") : QStringLiteral("FAIL"), zoomed ? QStringLiteral("ok") : QStringLiteral("FAIL"),
                         through ? QStringLiteral("ok") : QStringLiteral("FAIL"), throughOff ? QStringLiteral("ok") : QStringLiteral("FAIL"), restored ? QStringLiteral("ok") : QStringLiteral("FAIL"),
                         textOk ? QStringLiteral("ok") : QStringLiteral("FAIL"), clipboardOk ? QStringLiteral("ok") : QStringLiteral("FAIL"));
    return r;
}

ScenarioResult Runner::m4Scroll()
{
    // REQ: CAP-61 CAP-62 CAP-64 CAP-72 CAP-73 NFR-06 AC-09 AC-11
    ScenarioResult r{QStringLiteral("M4-SCROLL"), QStringLiteral("M4：自动滚动一个真实窗口（12 屏长）到底，长图与文档逐像素一致")};
    if (!desktopUnlocked())
        return skipped(r.id, r.title, QStringLiteral("锁屏中，无法模拟滚轮与显示测试窗口"));
    const QRect g = win::enumerateScreens().first().physicalGeometry;
    ScrollTestWindow window(480, 360, 360 * 12);
    if (!window.create(g.left() + 200, g.top() + 150)) {
        r.summary = QStringLiteral("无法创建测试窗口");
        return r;
    }
    QThread::msleep(300);
    window.pump();
    const QRect region = window.clientPhysicalRect();
    m_client.call(QStringLiteral("setting"), {{QStringLiteral("key"), QStringLiteral("scroll.speed")}, {QStringLiteral("value"), 3}});
    const QJsonObject start = m_client.call(QStringLiteral("scrollStart"), {{QStringLiteral("x"), region.x()}, {QStringLiteral("y"), region.y()}, {QStringLiteral("w"), region.width()},
                                                                            {QStringLiteral("h"), region.height()}, {QStringLiteral("auto"), true}});
    if (!start.value(QStringLiteral("ok")).toBool()) {
        r.summary = QStringLiteral("scrollStart 失败：") + start.value(QStringLiteral("error")).toString();
        return r;
    }
    // 会话由程序内定时器驱动；这里只泵测试窗口的消息并等待到底
    QElapsedTimer t;
    t.start();
    QJsonObject st;
    while (t.elapsed() < 60000) {
        window.pump();
        QThread::msleep(50);
        st = m_client.call(QStringLiteral("scrollState"));
        if (st.value(QStringLiteral("reachedEnd")).toBool())
            break;
    }
    const QString path = tempPng(QStringLiteral("m4-scroll"));
    const QJsonObject fin = m_client.call(QStringLiteral("scrollFinish"), {{QStringLiteral("path"), path}});
    m_client.call(QStringLiteral("scrollCancel"));
    const QImage out(path);
    const QImage doc = window.renderDocument();
    const auto cmp = testing::compareImages(out, doc);
    r.data = QJsonObject{{QStringLiteral("elapsedMs"), static_cast<double>(t.elapsed())}, {QStringLiteral("reachedEnd"), st.value(QStringLiteral("reachedEnd")).toBool()},
                         {QStringLiteral("frames"), st.value(QStringLiteral("frames")).toInt()}, {QStringLiteral("rejected"), st.value(QStringLiteral("rejected")).toInt()},
                         {QStringLiteral("height"), fin.value(QStringLiteral("h")).toInt()}, {QStringLiteral("docHeight"), doc.height()}, {QStringLiteral("pixels"), cmp.summary()}};
    r.passed = st.value(QStringLiteral("reachedEnd")).toBool() && cmp.passed;
    r.summary = QStringLiteral("到底=%1，%2 帧（拒绝 %3），长图 %4 px / 文档 %5 px，%6，耗时 %7 s")
                    .arg(st.value(QStringLiteral("reachedEnd")).toBool() ? QStringLiteral("ok") : QStringLiteral("FAIL")).arg(st.value(QStringLiteral("frames")).toInt())
                    .arg(st.value(QStringLiteral("rejected")).toInt()).arg(fin.value(QStringLiteral("h")).toInt()).arg(doc.height()).arg(cmp.summary()).arg(t.elapsed() / 1000.0, 0, 'f', 1);
    return r;
}

ScenarioResult Runner::startupTime()
{
    // REQ: NFR-03
    ScenarioResult r{QStringLiteral("NFR-03"), QStringLiteral("启动：进程创建到托盘与测试接口可用 ≤ 1.5 s")};
    const QString pipe = QStringLiteral("e2e-start-%1").arg(QCoreApplication::applicationPid());
    const QString config = QDir::temp().filePath(QStringLiteral("screenshot-e2e-start-%1.json").arg(QCoreApplication::applicationPid()));
    QProcess proc;
    proc.setProgram(m_options.appPath);
    proc.setArguments({QStringLiteral("--test-pipe"), pipe, QStringLiteral("--channel"), pipe, QStringLiteral("--config"), config, QStringLiteral("--allow-multiple")});
    QElapsedTimer t;
    t.start();
    proc.start();
    PipeClient client;
    const bool connected = proc.waitForStarted(5000) && client.connect(pipe, 15000);
    const qint64 ms = t.elapsed();
    if (connected)
        client.call(QStringLiteral("quit"));
    client.close();
    if (!proc.waitForFinished(5000))
        proc.kill();
    QFile::remove(config);
    r.data = QJsonObject{{QStringLiteral("startupMs"), static_cast<double>(ms)}, {QStringLiteral("connected"), connected}};
    r.passed = connected && ms <= 1500;
    r.summary = QStringLiteral("启动到接口可用 %1 ms（要求 ≤ 1500）").arg(ms);
    return r;
}

ScenarioResult Runner::cliWait()
{
    // REQ: AC-12 ACT-06
    ScenarioResult r{QStringLiteral("AC-12"), QStringLiteral("命令行：第二实例触发全屏截图并快速保存，加 --wait 时在保存完成后才退出")};
    QTemporaryDir dir;
    m_client.call(QStringLiteral("setting"), {{QStringLiteral("key"), QStringLiteral("output.quickSaveDir")}, {QStringLiteral("value"), dir.path()}});
    m_client.call(QStringLiteral("setting"), {{QStringLiteral("key"), QStringLiteral("output.fileNamePattern")}, {QStringLiteral("value"), QStringLiteral("cli_{n}")}});
    QProcess second;
    second.setProgram(m_options.appPath);
    second.setArguments({QStringLiteral("--channel"), m_options.channel, QStringLiteral("snip"), QStringLiteral("full"), QStringLiteral("--output"), QStringLiteral("quickSave"), QStringLiteral("--wait")});
    QElapsedTimer t;
    t.start();
    second.start();
    const bool exited = second.waitForFinished(20000);
    // 退出的瞬间文件必须已经存在（--wait 语义）
    const QStringList files = QDir(dir.path()).entryList({QStringLiteral("cli_*.png")}, QDir::Files);
    const QImage img(files.isEmpty() ? QString() : QDir(dir.path()).filePath(files.first()));
    // 用被测程序自己看到的物理显示器布局比对（驱动进程未声明 DPI 感知，本地枚举会被虚拟化）
    QRect desktop;
    bool sizeOk = false;
    for (const QJsonValue& v : m_client.call(QStringLiteral("screens")).value(QStringLiteral("screens")).toArray()) {
        const QJsonObject o = v.toObject();
        const QRect screenRect(o.value(QStringLiteral("x")).toInt(), o.value(QStringLiteral("y")).toInt(), o.value(QStringLiteral("w")).toInt(), o.value(QStringLiteral("h")).toInt());
        desktop |= screenRect;
        sizeOk = sizeOk || img.size() == screenRect.size(); // 全屏 = 当前显示器或全部显示器（按设置）
    }
    sizeOk = !img.isNull() && (sizeOk || img.size() == desktop.size());
    r.data = QJsonObject{{QStringLiteral("exited"), exited}, {QStringLiteral("exitCode"), second.exitCode()}, {QStringLiteral("files"), files.size()},
                         {QStringLiteral("elapsedMs"), static_cast<double>(t.elapsed())}, {QStringLiteral("imageSize"), QStringLiteral("%1x%2").arg(img.width()).arg(img.height())}};
    r.passed = exited && second.exitCode() == 0 && files.size() == 1 && sizeOk;
    r.summary = QStringLiteral("第二实例退出码 %1，退出时文件%2存在，尺寸 %3x%4（桌面 %5x%6）")
                    .arg(second.exitCode()).arg(files.isEmpty() ? QStringLiteral("不") : QStringLiteral("已")).arg(img.width()).arg(img.height()).arg(desktop.width()).arg(desktop.height());
    return r;
}

bool writeReport(const QString& path, const QList<ScenarioResult>& results, const QStringList& networkViolations)
{
    QJsonArray arr;
    bool all = true;
    for (const auto& r : results) {
        arr.append(QJsonObject{{QStringLiteral("id"), r.id}, {QStringLiteral("title"), r.title}, {QStringLiteral("passed"), r.passed},
                               {QStringLiteral("skipped"), r.skipped}, {QStringLiteral("summary"), r.summary}, {QStringLiteral("data"), r.data}});
        if (!r.passed && !r.skipped)
            all = false;
    }
    QJsonObject report{{QStringLiteral("scenarios"), arr}, {QStringLiteral("networkViolations"), QJsonArray::fromStringList(networkViolations)},
                       {QStringLiteral("allPassed"), all && networkViolations.isEmpty()}};
    if (!path.isEmpty()) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        if (f.open(QIODevice::WriteOnly))
            f.write(QJsonDocument(report).toJson(QJsonDocument::Indented));
    }
    logLine(QStringLiteral("==== 汇总"));
    for (const auto& r : results)
        logLine(QStringLiteral("%1 %2 %3").arg(r.skipped ? QStringLiteral("[跳过]") : r.passed ? QStringLiteral("[通过]") : QStringLiteral("[失败]"), r.id, r.summary));
    logLine(QStringLiteral("零网络：%1").arg(networkViolations.isEmpty() ? QStringLiteral("运行期间无任何网络端点") : networkViolations.join(QStringLiteral("; "))));
    return all && networkViolations.isEmpty();
}

} // namespace ss::driver
