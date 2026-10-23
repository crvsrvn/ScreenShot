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

    Options m_options;
    PipeClient& m_client;
    quint32 m_pid;
    QStringList m_networkViolations;
};

// 把结果写成 JSON 报告并在控制台打印摘要；返回是否全部通过
bool writeReport(const QString& path, const QList<ScenarioResult>& results, const QStringList& networkViolations);

} // namespace ss::driver
