#pragma once

#include "core/capture/CaptureHistory.h"
#include "core/code/CodeDecoder.h"
#include "core/config/Actions.h"
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
class PinManager;
class Settings;
class OutputActions;
class CommandServer;
class ScrollCaptureSession;

// 托盘与命令路由：持有平台层、设置、截图会话、输出动作和贴图管理。
class App : public QObject {
    Q_OBJECT
public:
    App(Platform platform, Settings& settings, QObject* parent = nullptr);
    ~App() override;

    enum HotkeyId { HotkeyCapture = 1, HotkeyPin = 2, HotkeyTogglePins = 3, HotkeyRestorePin = 4, HotkeyPassthrough = 5, HotkeyNextGroup = 6, HotkeyPreviousGroup = 7, HotkeyActionBase = 1000 };

    Platform& platform() { return m_platform; }
    Settings& settings() { return m_settings; }
    CaptureSession& session() { return *m_session; }
    OutputActions& output() { return *m_output; }
    CaptureHistory& history() { return m_history; }
    PinManager& pinManager() { return *m_pins; }
    ScrollCaptureSession* scrollSession() const { return m_scroll.get(); }
    // 开始滚动截图（CAP-60）；失败返回 false，原因在 scrollError()
    bool startScroll(const QRect& physicalRegion, bool showUi = true);
    QString scrollError() const { return m_scrollError; }
    QList<PinWindow*> pins() const;
    QHash<int, bool> hotkeyStatus() const { return m_hotkeyStatus; }
    bool hotkeysEnabled() const;
    bool pinsHidden() const;

    void useScreenCapture(std::unique_ptr<IScreenCapture> capture);
    void startCommandServer(const QString& channelName);
    // 命令行接口（ACT-06）；requestId 非 0 时完成后经 CommandServer::reply 应答
    void runCommand(const QStringList& command, int requestId = 0);
    // 动作（ACT-03）
    QList<Action> actions() const;
    bool runAction(const QString& name);
    void showActionsDialog();
    // 识别（REC-01、REC-06）
    void showOcrResult(const QString& text);
    void showCodeResult(const QList<CodeResult>& codes);
    void recognizeTextFromFile(const QString& path = QString());
    void decodeCodesFromFile(const QString& path = QString());
    void notifyCrashDumps(const QStringList& dumps);
    QMenu* trayMenu() const { return m_trayMenu; }
    void trayActivated(int reason);
    void checkPermissions();
    // 当前光标的物理位置（贴图放置位置）
    static QPoint cursorPhysical();

    // 快捷截图（CAP-06）：不交互，直接按设置的动作输出。kind：full | last | activeWindow | activeScreen | cursorScreen
    bool quickCapture(const QString& kind);
    // 指定区域（CAP-05 自定义截图 / 命令行）：delayMs 后截图并输出；interactive=false 时不显示界面
    void captureRegion(const QRect& physical, int delayMs = 0, bool interactive = false);
    void showCustomCaptureDialog();
    void startWhiteboard();

public slots:
    void startCapture(qint64 triggerNs);
    PinWindow* pinImage(const QImage& image, QPoint physicalPos);
    void pinFromClipboard();   // 贴图热键语义：剪贴板有新内容则贴，否则恢复已关闭
    void restoreClosedPin();
    void togglePinsHidden();
    void togglePassthroughAtCursor();
    void restoreOffscreenPins();
    void closeAllPins();
    void setHotkeysEnabled(bool enabled);
    void showPreferences();
    void showAbout();
    void quit();

private:
    void setupTray();
    void setupHotkeys();
    void onSizeInputRequested();
    void onOcrCopyRequested(const QImage& image);
    void onCodesRequested(const QList<CodeResult>& codes);
    void registerHotkeys();
    void rebuildActionsMenu();
    void finishCommand(int requestId, const QString& text);
    void applySetting(const QString& key);

    Platform m_platform;
    Settings& m_settings;
    std::unique_ptr<IScreenCapture> m_injectedCapture;
    std::unique_ptr<OutputActions> m_output;
    std::unique_ptr<CaptureSession> m_session;
    std::unique_ptr<PinManager> m_pins;
    std::unique_ptr<CommandServer> m_commands;
    std::unique_ptr<ScrollCaptureSession> m_scroll;
    QImage m_scrollResult;
    QRect m_scrollRegion;
    QString m_scrollError;
    CaptureHistory m_history;
    QHash<int, bool> m_hotkeyStatus;
    QSystemTrayIcon* m_tray = nullptr;
    QMenu* m_trayMenu = nullptr;
    QAction* m_hotkeysAction = nullptr;
    QAction* m_togglePinsAction = nullptr;
    QMenu* m_groupMenu = nullptr;
    QMenu* m_actionsMenu = nullptr;
    int m_pendingReply = 0; // 等待截图完成后应答的命令
    int m_actionHotkeys = 0;
    QRect quickRegion(const QString& kind);
    void rebuildGroupMenu();
};

} // namespace ss
