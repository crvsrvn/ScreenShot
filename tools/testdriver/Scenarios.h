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
