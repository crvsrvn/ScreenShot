#pragma once

#include "core/capture/CaptureHistory.h"
#include "core/ports/Platform.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <memory>

class QSystemTrayIcon;
class QMenu;
class QAction;

namespace ss {

class CaptureSession;
class PinWindow;
class Settings;
class OutputActions;
class CommandServer;

// 托盘与命令路由：持有平台层、设置、截图会话、输出动作和贴图列表。
class App : public QObject {
    Q_OBJECT
public:
    App(Platform platform, Settings& settings, QObject* parent = nullptr);
    ~App() override;

    enum HotkeyId { HotkeyCapture = 1, HotkeyPin = 2, HotkeyTogglePins = 3 };

    Platform& platform() { return m_platform; }
    Settings& settings() { return m_settings; }
    CaptureSession& session() { return *m_session; }
    OutputActions& output() { return *m_output; }
    CaptureHistory& history() { return m_history; }
    QList<PinWindow*>& pins() { return m_pins; }
    QHash<int, bool> hotkeyStatus() const { return m_hotkeyStatus; }
    bool hotkeysEnabled() const;
    bool pinsHidden() const { return m_pinsHidden; }

    // 用指定捕获实现替换会话使用的捕获（测试接口注入合成画面）；传空恢复真实捕获
    void useScreenCapture(std::unique_ptr<IScreenCapture> capture);
    // 单实例命令通道（GEN-01）
    void startCommandServer(const QString& channelName);
    // 执行一条命令行命令（snip / pin / toggle-pins / prefs / quit）
    void runCommand(const QStringList& command);
    // 启动时提示上次崩溃（GEN-09）
    void notifyCrashDumps(const QStringList& dumps);
    // 托盘菜单与点击映射（GEN-02、GEN-03）；菜单在没有系统托盘时也会构建，便于测试
    QMenu* trayMenu() const { return m_trayMenu; }
    void trayActivated(int reason);
    // macOS 首次运行权限引导（GEN-14）；其他平台无操作
    void checkPermissions();

public slots:
    void startCapture(qint64 triggerNs);
    PinWindow* pinImage(const QImage& image, QPoint physicalPos);
    void pinFromClipboard();
    void togglePinsHidden();
    void restoreOffscreenPins();
    void closeAllPins();
    void setHotkeysEnabled(bool enabled);
    void showPreferences();
    void showAbout();
    void quit();

private:
    void setupTray();
    void setupHotkeys();
    void registerHotkeys();
    void applySetting(const QString& key);
    void onCaptured(const QImage& image, const QRect& physicalRegion);

    Platform m_platform;
    Settings& m_settings;
    std::unique_ptr<IScreenCapture> m_injectedCapture;
    std::unique_ptr<OutputActions> m_output;
    std::unique_ptr<CaptureSession> m_session;
    std::unique_ptr<CommandServer> m_commands;
    CaptureHistory m_history;
    QList<PinWindow*> m_pins;
    QHash<int, bool> m_hotkeyStatus;
    bool m_pinsHidden = false;
    QSystemTrayIcon* m_tray = nullptr;
    QMenu* m_trayMenu = nullptr;
    QAction* m_hotkeysAction = nullptr;
    QAction* m_togglePinsAction = nullptr;
};

} // namespace ss
