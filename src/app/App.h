#pragma once

#include "core/ports/Platform.h"

#include <QObject>
#include <QList>
#include <memory>

class QSystemTrayIcon;
class QMenu;

namespace ss {

class CaptureSession;
class PinWindow;

// 托盘与命令路由：持有平台层、截图会话和贴图列表。
class App : public QObject {
    Q_OBJECT
public:
    explicit App(Platform platform, QObject* parent = nullptr);
    ~App() override;

    enum HotkeyId { HotkeyCapture = 1, HotkeyPin = 2 };

    Platform& platform() { return m_platform; }
    CaptureSession& session() { return *m_session; }
    QList<PinWindow*>& pins() { return m_pins; }

    // 用指定捕获实现替换会话使用的捕获（测试接口注入合成画面）；传空恢复真实捕获
    void useScreenCapture(std::unique_ptr<IScreenCapture> capture);

public slots:
    void startCapture(qint64 triggerNs);
    PinWindow* pinImage(const QImage& image, QPoint physicalPos);
    void pinFromClipboard();
    void closeAllPins();
    void quit();

private:
    void setupTray();
    void setupHotkeys();
    void onCaptured(const QImage& image, const QRect& physicalRegion);

    Platform m_platform;
    std::unique_ptr<IScreenCapture> m_injectedCapture;
    std::unique_ptr<CaptureSession> m_session;
    QList<PinWindow*> m_pins;
    QSystemTrayIcon* m_tray = nullptr;
    QMenu* m_trayMenu = nullptr;
};

} // namespace ss
