#pragma once

#include "core/capture/CaptureHistory.h"
#include "core/code/CodeDecoder.h"
#include "core/ports/IOcrEngine.h"

#ifdef Q_OS_WIN
namespace ss::win { class WinInputHooks; }
#endif
#include "core/ports/Platform.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <memory>

class QSystemTrayIcon;
class QMenu;

namespace ss {

class CaptureSession;
class PinWindow;
class PinManager;
class Settings;
class OutputActions;
class CommandServer;
class ScrollCaptureSession;
class RecordSession;
class RecordPlaybackDialog;
class PreferencesDialog;

// 托盘与命令路由：持有平台层、设置、截图会话、输出动作和贴图管理。
class App : public QObject {
    Q_OBJECT
public:
    App(Platform platform, Settings& settings, QObject* parent = nullptr);
    ~App() override;

    enum HotkeyId { HotkeyCapture = 1, HotkeyPin = 2, HotkeyTogglePins = 3, HotkeyRestorePin = 4, HotkeyPassthrough = 5 };

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
    // 屏幕录制（VID-01）：准备录制该区域；失败返回 false，原因在 recordError()
    bool startRecord(const QRect& physicalRegion, bool showUi = true);
    RecordSession* recordSession() const { return m_record.get(); }
    QString recordError() const { return m_recordError; }
    RecordPlaybackDialog* playbackDialog() const;
    QString recordLastFile() const { return m_recordLastFile; }
    // 异常退出后遗留的未完成录制目录（VID-09）；recoverRecordings 询问后打开或删除
    QStringList unfinishedRecordings() const;
    RecordPlaybackDialog* openRecording(const QString& dir);
    int recoverRecordings();
    QList<PinWindow*> pins() const;
    QHash<int, bool> hotkeyStatus() const { return m_hotkeyStatus; }
    bool pinsHidden() const;

    void useScreenCapture(std::unique_ptr<IScreenCapture> capture);
    void startCommandServer(const QString& channelName);
    // 命令行接口（ACT-06）；requestId 非 0 时完成后经 CommandServer::reply 应答
    QString runCommand(const QStringList& command, int requestId = 0);
    // 识别（REC-01、REC-06）
    void showOcrResult(const QString& text, const QList<OcrLine>& lines = {});
    QString formatOcr(const OcrResult& r) const;
    OcrResult recognizeLines(const QImage& image);
    void applyOcrOptions();
    void showCodeResult(const QList<CodeResult>& codes);
    void recognizeTextFromFile(const QString& path = QString());
    void decodeCodesFromFile(const QString& path = QString());
    void notifyCrashDumps(const QStringList& dumps);
    QMenu* trayMenu() const { return m_trayMenu; }
    void trayActivated(int reason); // 左键截图、中键贴图
    void checkPermissions();
    // 数据目录（配置文件所在目录）：贴图恢复（PIN-54）
    void setDataDir(const QString& dir) { m_dataDir = dir; }
    int restorePins();
    // 当前光标的物理位置（贴图放置位置）
    static QPoint cursorPhysical();

    // 快捷截图（CAP-06）：不交互，直接按设置的动作输出。kind：full | last | activeWindow | activeScreen | cursorScreen
    bool quickCapture(const QString& kind);
    // 指定区域（命令行）：delayMs 后截图并输出；interactive=false 时不显示界面
    void captureRegion(const QRect& physical, int delayMs = 0, bool interactive = false);

public slots:
    void startCapture(qint64 triggerNs);
    PinWindow* pinImage(const QImage& image, QPoint physicalPos);
    void pinFromClipboard();   // 贴图热键语义：截图中贴选区；否则剪贴板有新内容则贴，没有则恢复已关闭
    void restoreClosedPin();
    void togglePinsHidden();
    void togglePassthroughAtCursor();
    void restoreOffscreenPins();
    void closeAllPins();
    void showPreferences();
    void quit();

private:
    void setupTray();
#ifdef Q_OS_WIN
    void showTrayMenu();
#endif
    void setupHotkeys();
    void onSizeInputRequested();
    void onOcrCopyRequested(const QImage& image);
    void onCodesRequested(const QList<CodeResult>& codes);
    void registerHotkeys();
    void loadHistoryDir();
    void applyInputHooks();
    void finishCommand(int requestId, const QString& text);
    void applySetting(const QString& key);
    void finishRecording();

    Platform m_platform;
    Settings& m_settings;
    std::unique_ptr<IScreenCapture> m_injectedCapture;
    std::unique_ptr<OutputActions> m_output;
    std::unique_ptr<CaptureSession> m_session;
    std::unique_ptr<PinManager> m_pins;
    std::unique_ptr<CommandServer> m_commands;
    std::unique_ptr<ScrollCaptureSession> m_scroll;
    std::unique_ptr<RecordSession> m_record;
    QString m_recordError;
    QString m_recordLastFile;
    bool m_recordHooks = false;
    QPointer<RecordPlaybackDialog> m_playback;
    QPointer<PreferencesDialog> m_preferences; // 只开一个设置窗口
    QImage m_scrollResult;
public:
    QSystemTrayIcon* trayIcon() const { return m_tray; }
private:
    QRect m_scrollRegion;
    QString m_scrollError;
    CaptureHistory m_history;
    QHash<int, bool> m_hotkeyStatus;
    QSystemTrayIcon* m_tray = nullptr;
    QMenu* m_trayMenu = nullptr;
    int m_pendingReply = 0; // 等待截图完成后应答的命令
    QString m_dataDir;
#ifdef Q_OS_WIN
    std::unique_ptr<win::WinInputHooks> m_hooks; // 录制时的输入可视化（VID-06）
#endif
    QRect quickRegion(const QString& kind);
};

} // namespace ss
