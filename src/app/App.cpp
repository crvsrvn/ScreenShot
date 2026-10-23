#include "app/App.h"

#include "app/CaptureSession.h"
#include "app/CommandServer.h"
#include "app/OutputActions.h"
#include "app/PinManager.h"
#include "app/PinWindow.h"
#include "app/PreferencesDialog.h"
#include "core/config/Settings.h"
#include "core/util/Clock.h"

#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QGuiApplication>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QScreen>
#include <QSystemTrayIcon>

#if defined(Q_OS_WIN)
#include "platform/windows/WinSystem.h"
#include "platform/windows/WinWindowTools.h"
#elif defined(Q_OS_MACOS)
#include "platform/macos/MacPermissions.h"
#include "platform/macos/MacWindowTools.h"
#include <QPushButton>
#endif

namespace ss {

namespace {

QIcon makeTrayIcon()
{
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
    , m_pins(std::make_unique<PinManager>(settings, m_output.get()))
    , m_history(settings.intValue(QStringLiteral("capture.historyLimit")))
{
    m_session->setHistory(&m_history);
    m_session->setOutput(m_output.get());
    connect(m_session.get(), &CaptureSession::pinRequested, this, [this](const QImage& image, const QRect& region) { pinImage(image, region.topLeft()); });
    connect(&m_settings, &Settings::changed, this, [this](const QString& key, const QVariant&) { applySetting(key); });
    m_session->prepareOverlays();
    setupTray();
    setupHotkeys();
}

App::~App()
{
    delete m_trayMenu;
}

QPoint App::cursorPhysical()
{
#if defined(Q_OS_WIN)
    return win::cursorPhysicalPos();
#elif defined(Q_OS_MACOS)
    return mac::cursorPhysicalPos();
#else
    return QCursor::pos();
#endif
}

QList<PinWindow*> App::pins() const
{
    return m_pins->pins();
}

bool App::pinsHidden() const
{
    return m_pins->allHidden();
}

void App::checkPermissions()
{
#ifdef Q_OS_MACOS
    if (!mac::hasScreenRecordingPermission()) {
        mac::requestScreenRecordingPermission();
        QMessageBox box(QMessageBox::Information, tr("Screen recording permission"),
                        tr("ScreenShot needs the Screen Recording permission to capture the screen. Grant it in System Settings, then restart ScreenShot."));
        QPushButton* open = box.addButton(tr("Open System Settings"), QMessageBox::ActionRole);
        box.addButton(QMessageBox::Close);
        box.exec();
        if (box.clickedButton() == open)
            mac::openScreenRecordingSettings();
    }
    if (!mac::hasAccessibilityPermission())
        mac::requestAccessibilityPermission(true);
#endif
}

void App::setupTray()
{
    m_trayMenu = new QMenu();
    m_trayMenu->addAction(tr("Capture\tF1"), this, [this] { startCapture(monotonicNs()); });
    m_trayMenu->addAction(tr("Pin clipboard\tF3"), this, &App::pinFromClipboard);
    m_togglePinsAction = m_trayMenu->addAction(tr("Hide all pins\tShift+F3"), this, &App::togglePinsHidden);
    m_trayMenu->addAction(tr("Restore closed pin\tCtrl+3"), this, &App::restoreClosedPin);
    m_trayMenu->addAction(tr("Restore off-screen pins"), this, &App::restoreOffscreenPins);
    m_trayMenu->addSeparator();
    m_hotkeysAction = m_trayMenu->addAction(tr("Disable hotkeys"), this, [this](bool checked) { setHotkeysEnabled(!checked); });
    m_hotkeysAction->setCheckable(true);
    m_hotkeysAction->setChecked(!hotkeysEnabled());
    m_trayMenu->addAction(tr("Preferences..."), this, &App::showPreferences);
    m_trayMenu->addAction(tr("Help"), this, &App::showAbout);
    m_trayMenu->addSeparator();
    m_trayMenu->addAction(tr("Quit"), this, &App::quit);
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;
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
        switch (id) {
        case HotkeyCapture: startCapture(atNs); break;
        case HotkeyPin: pinFromClipboard(); break;
        case HotkeyTogglePins: togglePinsHidden(); break;
        case HotkeyRestorePin: restoreClosedPin(); break;
        case HotkeyPassthrough: togglePassthroughAtCursor(); break;
        default: break;
        }
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
    for (int id : {HotkeyCapture, HotkeyPin, HotkeyTogglePins, HotkeyRestorePin, HotkeyPassthrough})
        m_platform.hotkey->unregisterHotkey(id);
    m_hotkeyStatus.clear();
    if (!hotkeysEnabled())
        return;
    const QList<QPair<int, QString>> keys{{HotkeyCapture, QStringLiteral("hotkey.capture")}, {HotkeyPin, QStringLiteral("hotkey.pin")},
                                          {HotkeyTogglePins, QStringLiteral("hotkey.togglePins")}, {HotkeyRestorePin, QStringLiteral("hotkey.restorePin")},
                                          {HotkeyPassthrough, QStringLiteral("hotkey.togglePassthrough")}};
    for (const auto& [id, settingKey] : keys) {
        const QKeySequence seq(m_settings.stringValue(settingKey));
        const bool ok = !seq.isEmpty() && m_platform.hotkey->registerHotkey(id, seq);
        m_hotkeyStatus[id] = ok;
        if (!ok && !seq.isEmpty()) {
            qWarning("hotkey %s (%s) could not be registered", qPrintable(settingKey), qPrintable(seq.toString()));
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

PinWindow* App::pinImage(const QImage& image, QPoint physicalPos)
{
    return m_pins->pinImage(image, physicalPos);
}

void App::pinFromClipboard()
{
    m_pins->pinHotkey(cursorPhysical());
}

void App::restoreClosedPin()
{
    m_pins->restoreLastClosed();
}

void App::togglePinsHidden()
{
    m_pins->toggleHidden();
    if (m_togglePinsAction)
        m_togglePinsAction->setText(m_pins->allHidden() ? tr("Show all pins\tShift+F3") : tr("Hide all pins\tShift+F3"));
}

void App::togglePassthroughAtCursor()
{
    m_pins->togglePassthroughAt(cursorPhysical());
}

void App::restoreOffscreenPins()
{
    m_pins->restoreOffscreen();
}

void App::closeAllPins()
{
    m_pins->destroyAll();
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
