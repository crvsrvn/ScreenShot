#pragma once

#include "PipeClient.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <functional>

namespace ss::driver {

struct Options {
    QString appPath;
    QString reportPath;
    bool quick = false;      // 缩短耗时项（迭代次数、空闲等待）
    QString only;            // 只运行某个场景（如 R-02）
    QString channel;         // 被测程序的单实例通道名
};

// 一个场景的结论
struct ScenarioResult {
    QString id;        // R-xx
    QString title;
    bool passed = false;
    bool skipped = false;
    QString summary;
    QJsonObject data;
};

class Runner {
public:
    Runner(Options options, PipeClient& client, quint32 appPid);

    QList<ScenarioResult> runAll();
    ScenarioResult run(const QString& id);

    // 零网络监控结果
    QStringList networkViolations() const { return m_networkViolations; }
    void pollNetwork();

    // 交互桌面是否可用（未锁屏）。锁屏时模拟输入到不了程序窗口，相关场景只能跳过。
    static bool desktopUnlocked();
    // 桌面状态说明（可用 / 锁屏 / 会话已断开），写进日志帮助判断为什么场景被跳过
    static QString desktopStatus();

private:
    ScenarioResult r01Toolchain();
    ScenarioResult r02CaptureLatency();
    ScenarioResult r03MixedScaleAccuracy();
    ScenarioResult r05PinPerformance();
    ScenarioResult r06BlurMosaic();
    ScenarioResult r07LargePinScaling();
    ScenarioResult r08IdleMemory();
    ScenarioResult r09ElementDetectorHang();
    ScenarioResult r11WindowsOcr();
    ScenarioResult r16FakeMultiScreen();
    ScenarioResult r17QmlNetworkDependency();
    ScenarioResult hotkeyEndToEnd();
    ScenarioResult m1CaptureFlow();     // 框选 → 快速保存 → 文件与选区一致
    ScenarioResult m1SingleInstance();  // 第二个实例把 snip 转交给运行中的实例
    ScenarioResult m2Annotate();        // 真实桌面上画一个填充矩形并快速保存
    ScenarioResult m3Pins();            // 贴图：物理定位、缩放、穿透、关闭 / 恢复、文本贴图
    ScenarioResult m4Scroll();          // 真实窗口上的自动滚动长截图，与文档逐像素比对（AC-11）
    ScenarioResult startupTime();       // 冷启动到测试接口可用的时间（NFR-03）
    ScenarioResult cliWait();           // 命令行触发全屏截图并等待完成（AC-12）

    // 每个场景开始前把程序恢复到空闲状态（取消截图、关闭贴图）
    void resetApp();
    ScenarioResult skipped(const QString& id, const QString& title, const QString& reason);

    Options m_options;
    PipeClient& m_client;
    quint32 m_pid;
    QStringList m_networkViolations;
};

// 把结果写成 JSON 报告并在控制台打印摘要；返回是否全部通过
bool writeReport(const QString& path, const QList<ScenarioResult>& results, const QStringList& networkViolations);

} // namespace ss::driver
