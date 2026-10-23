#include "app/App.h"

#include "app/CaptureSession.h"
#include "app/CommandServer.h"
#include "app/OutputActions.h"
#include "app/PinWindow.h"
#include "app/PreferencesDialog.h"
#include "core/config/Settings.h"
#include "core/util/Clock.h"

#include <QApplication>
#include <QClipboard>
#include <QGuiApplication>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QScreen>
#include <QSystemTrayIcon>

#ifdef Q_OS_WIN
#include "platform/windows/WinSystem.h"
#endif

namespace ss {

namespace {

QIcon makeTrayIcon()
{
    // 自行设计的简单图标：圆角方框 + 取景圆点
    QPixmap pm(64, 64);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(40, 120, 220), 6));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(8, 8, 48, 48), 8, 8);
    p.setBrush(QColor(40, 120, 220));
    p.setPen(Qt::NoPen);
    p.drawEllipse(QPointF(32, 32), 9, 9);
    return QIcon(pm);
}

} // namespace

App::App(Platform platform, Settings& settings, QObject* parent)
    : QObject(parent)
    , m_platform(std::move(platform))
    , m_settings(settings)
    , m_output(std::make_unique<OutputActions>(settings))
    , m_session(std::make_unique<CaptureSession>(m_platform.screenCapture.get(), m_platform.windowDetector.get(), m_platform.elementDetector.get(), settings))
    , m_history(settings.intValue(QStringLiteral("capture.historyLimit")))
{
    m_session->setHistory(&m_history);
    m_session->setOutput(m_output.get());
    connect(m_session.get(), &CaptureSession::captured, this, &App::onCaptured);
    connect(m_session.get(), &CaptureSession::pinRequested, this, [this](const QImage& image, const QRect& region) { pinImage(image, region.topLeft()); });
    connect(&m_settings, &Settings::changed, this, [this](const QString& key, const QVariant&) { applySetting(key); });
    // 覆盖窗口预先创建，触发时只换背景图（NFR-01）
    m_session->prepareOverlays();
    setupTray();
    setupHotkeys();
}

App::~App()
{
    closeAllPins();
}

void App::setupTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;
    m_trayMenu = new QMenu();
    m_trayMenu->addAction(tr("Capture\tF1"), this, [this] { startCapture(monotonicNs()); });
    m_trayMenu->addAction(tr("Pin clipboard\tF3"), this, &App::pinFromClipboard);
    m_togglePinsAction = m_trayMenu->addAction(tr("Hide all pins\tShift+F3"), this, &App::togglePinsHidden);
    m_trayMenu->addAction(tr("Restore off-screen pins"), this, &App::restoreOffscreenPins);
    m_trayMenu->addSeparator();
    m_hotkeysAction = m_trayMenu->addAction(tr("Disable hotkeys"), this, [this](bool checked) { setHotkeysEnabled(!checked); });
    m_hotkeysAction->setCheckable(true);
    m_hotkeysAction->setChecked(!hotkeysEnabled());
    m_trayMenu->addAction(tr("Preferences..."), this, &App::showPreferences);
    m_trayMenu->addAction(tr("Help"), this, &App::showAbout);
    m_trayMenu->addSeparator();
    m_trayMenu->addAction(tr("Quit"), this, &App::quit);
    m_tray = new QSystemTrayIcon(makeTrayIcon(), this);
    m_tray->setToolTip(QStringLiteral("ScreenShot"));
    m_tray->setContextMenu(m_trayMenu);
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) { trayActivated(reason); });
    m_tray->show();
}

void App::trayActivated(int reason)
{
    QString action;
    if (reason == QSystemTrayIcon::Trigger)
        action = m_settings.stringValue(QStringLiteral("general.trayLeftClick"));
    else if (reason == QSystemTrayIcon::MiddleClick)
        action = m_settings.stringValue(QStringLiteral("general.trayMiddleClick"));
    else
        return;
    if (action == QLatin1String("capture"))
        startCapture(monotonicNs());
    else if (action == QLatin1String("pin"))
        pinFromClipboard();
    else if (action == QLatin1String("menu") && m_trayMenu)
        m_trayMenu->popup(QCursor::pos());
}

void App::setupHotkeys()
{
    if (!m_platform.hotkey)
        return;
    connect(m_platform.hotkey.get(), &IGlobalHotkey::triggered, this, [this](int id, qint64 atNs) {
        if (id == HotkeyCapture)
            startCapture(atNs);
        else if (id == HotkeyPin)
            pinFromClipboard();
        else if (id == HotkeyTogglePins)
            togglePinsHidden();
    });
    registerHotkeys();
}

bool App::hotkeysEnabled() const
{
    return m_settings.boolValue(QStringLiteral("hotkey.enabled"));
}

void App::registerHotkeys()
{
    if (!m_platform.hotkey)
        return;
    for (int id : {HotkeyCapture, HotkeyPin, HotkeyTogglePins})
        m_platform.hotkey->unregisterHotkey(id);
    m_hotkeyStatus.clear();
    if (!hotkeysEnabled())
        return;
    const QHash<int, QString> keys{{HotkeyCapture, QStringLiteral("hotkey.capture")}, {HotkeyPin, QStringLiteral("hotkey.pin")}, {HotkeyTogglePins, QStringLiteral("hotkey.togglePins")}};
    for (auto it = keys.cbegin(); it != keys.cend(); ++it) {
        const QKeySequence seq(m_settings.stringValue(it.value()));
        const bool ok = !seq.isEmpty() && m_platform.hotkey->registerHotkey(it.key(), seq);
        m_hotkeyStatus[it.key()] = ok;
        if (!ok && !seq.isEmpty()) {
            qWarning("hotkey %s (%s) could not be registered", qPrintable(it.value()), qPrintable(seq.toString()));
            if (m_tray)
                m_tray->showMessage(tr("Hotkey conflict"), tr("%1 is already used by another program.").arg(seq.toString(QKeySequence::NativeText)), QSystemTrayIcon::Warning);
        }
    }
}

void App::setHotkeysEnabled(bool enabled)
{
    m_settings.setValue(QStringLiteral("hotkey.enabled"), enabled);
}

void App::applySetting(const QString& key)
{
    if (key.startsWith(QStringLiteral("hotkey."))) {
        registerHotkeys();
        if (m_hotkeysAction)
            m_hotkeysAction->setChecked(!hotkeysEnabled());
    } else if (key == QStringLiteral("capture.historyLimit")) {
        m_history.setLimit(m_settings.intValue(key));
    } else if (key == QStringLiteral("general.autostart")) {
#ifdef Q_OS_WIN
        win::setAutostart(m_settings.boolValue(key), QCoreApplication::applicationFilePath(), QStringLiteral("ScreenShot"));
#endif
    }
    m_settings.saveToFile();
}

void App::useScreenCapture(std::unique_ptr<IScreenCapture> capture)
{
    m_injectedCapture = std::move(capture);
    m_session->setScreenCapture(m_injectedCapture ? m_injectedCapture.get() : m_platform.screenCapture.get());
}

void App::startCommandServer(const QString& channelName)
{
    m_commands = std::make_unique<CommandServer>(channelName, this);
    connect(m_commands.get(), &CommandServer::commandReceived, this, &App::runCommand);
    m_commands->start();
}

void App::runCommand(const QStringList& command)
{
    const QString name = command.value(0);
    if (name == QLatin1String("snip"))
        startCapture(monotonicNs());
    else if (name == QLatin1String("pin"))
        pinFromClipboard();
    else if (name == QLatin1String("toggle-pins"))
        togglePinsHidden();
    else if (name == QLatin1String("prefs"))
        showPreferences();
    else if (name == QLatin1String("quit"))
        quit();
    else if (!name.isEmpty())
        qWarning("unknown command: %s", qPrintable(name));
}

void App::notifyCrashDumps(const QStringList& dumps)
{
    if (dumps.isEmpty() || !m_tray)
        return;
    m_tray->showMessage(tr("ScreenShot crashed last time"), tr("A crash dump was written to %1").arg(dumps.first()), QSystemTrayIcon::Warning);
}

void App::startCapture(qint64 triggerNs)
{
    m_session->start(triggerNs);
}

void App::onCaptured(const QImage& image, const QRect& physicalRegion)
{
    Q_UNUSED(image);
    Q_UNUSED(physicalRegion);
}

PinWindow* App::pinImage(const QImage& image, QPoint physicalPos)
{
    auto* pin = new PinWindow(image);
    pin->moveToPhysical(physicalPos);
    pin->fitToScreen();
    if (!m_pinsHidden)
        pin->show();
    m_pins.append(pin);
    connect(pin, &PinWindow::closed, this, [this, pin] {
        m_pins.removeOne(pin);
        pin->deleteLater();
    });
    return pin;
}

void App::pinFromClipboard()
{
    const QImage image = QApplication::clipboard()->image();
    if (image.isNull())
        return;
    m_pinsHidden = false; // 新建贴图时自动重新显示（PIN-52）
    for (auto* p : std::as_const(m_pins))
        p->show();
    pinImage(image, QCursor::pos());
}

void App::togglePinsHidden()
{
    m_pinsHidden = !m_pinsHidden;
    for (auto* p : std::as_const(m_pins))
        p->setVisible(!m_pinsHidden);
    if (m_togglePinsAction)
        m_togglePinsAction->setText(m_pinsHidden ? tr("Show all pins\tShift+F3") : tr("Hide all pins\tShift+F3"));
}

void App::restoreOffscreenPins()
{
    // 不在任何屏幕上的贴图挪回主屏（PIN-55）
    QRect desktop;
    for (QScreen* s : QGuiApplication::screens())
        desktop = desktop.united(s->geometry());
    for (auto* p : std::as_const(m_pins)) {
        if (!desktop.intersects(p->contentRect().toRect()))
            p->moveContentTo(QGuiApplication::primaryScreen()->geometry().center() - QPointF(p->contentRect().width() / 2, p->contentRect().height() / 2));
    }
}

void App::closeAllPins()
{
    const auto pins = m_pins;
    m_pins.clear();
    for (auto* p : pins) {
        p->hide();
        delete p;
    }
}

void App::showPreferences()
{
    PreferencesDialog dialog(m_settings);
    dialog.exec();
    m_settings.saveToFile();
}

void App::showAbout()
{
    QMessageBox::about(nullptr, tr("About ScreenShot"),
                       tr("<b>ScreenShot</b><br>Screenshot and pin tool.<br><br>Capture: %1<br>Pin clipboard: %2<br>Hide / show pins: %3<br><br>"
                          "While capturing: drag to select, Enter / double-click to copy, Ctrl+S to save, Ctrl+Shift+S to quick save, "
                          "Ctrl+T to pin, Esc to cancel, Tab to switch window / element detection, C to copy the pixel color, "
                          "WASD and arrow keys to adjust by one pixel, , and . to recall previous captures.")
                           .arg(m_settings.stringValue(QStringLiteral("hotkey.capture")), m_settings.stringValue(QStringLiteral("hotkey.pin")),
                                m_settings.stringValue(QStringLiteral("hotkey.togglePins"))));
}

void App::quit()
{
    m_settings.saveToFile();
    QApplication::quit();
}

} // namespace ss
