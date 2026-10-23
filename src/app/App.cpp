#include "app/App.h"

#include "app/CaptureSession.h"
#include "app/CommandServer.h"
#include "app/CustomCaptureDialog.h"
#include "app/OutputActions.h"
#include "app/PinManager.h"
#include "app/PinWindow.h"
#include "app/PreferencesDialog.h"
#include "app/ScrollCaptureSession.h"
#include "core/config/Settings.h"
#include "core/util/Clock.h"

#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QInputDialog>
#include <QRegularExpression>
#include <QTimer>
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
    connect(m_session.get(), &CaptureSession::scrollRequested, this, [this](const QRect& region) { startScroll(region); });
    connect(m_session.get(), &CaptureSession::sizeInputRequested, this, &App::onSizeInputRequested);
    connect(m_session.get(), &CaptureSession::ocrCopyRequested, this, &App::onOcrCopyRequested);
    connect(m_session.get(), &CaptureSession::captured, this, [this](const QImage& image, const QRect& region) {
        m_settings.setValue(QStringLiteral("capture.lastRegion"), QStringLiteral("%1,%2,%3,%4").arg(region.x()).arg(region.y()).arg(region.width()).arg(region.height()));
        m_output->autoSave(image); // OUT-09：截图成功时额外存一份
    });
    m_output->cleanupAutoSave();
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
    m_trayMenu->addAction(tr("Custom capture..."), this, &App::showCustomCaptureDialog);
    QMenu* quick = m_trayMenu->addMenu(tr("Quick capture"));
    quick->addAction(tr("Full screen"), this, [this] { quickCapture(QStringLiteral("full")); });
    quick->addAction(tr("Last area"), this, [this] { quickCapture(QStringLiteral("last")); });
    quick->addAction(tr("Active window"), this, [this] { quickCapture(QStringLiteral("activeWindow")); });
    quick->addAction(tr("Active screen"), this, [this] { quickCapture(QStringLiteral("activeScreen")); });
    quick->addAction(tr("Screen under cursor"), this, [this] { quickCapture(QStringLiteral("cursorScreen")); });
    m_trayMenu->addAction(tr("Whiteboard"), this, &App::startWhiteboard);
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
    if (name == QLatin1String("snip")) {
        // snip [full|last|active-window|active-screen|cursor-screen|custom|whiteboard|x,y,w,h] [--delay ms]
        const QString arg = command.value(1);
        int delay = 0;
        const int d = static_cast<int>(command.indexOf(QStringLiteral("--delay")));
        if (d >= 0)
            delay = command.value(d + 1).toInt();
        if (arg.isEmpty() || arg.startsWith(QStringLiteral("--")))
            startCapture(monotonicNs());
        else if (arg == QLatin1String("full") || arg == QLatin1String("last"))
            quickCapture(arg);
        else if (arg == QLatin1String("active-window"))
            quickCapture(QStringLiteral("activeWindow"));
        else if (arg == QLatin1String("active-screen"))
            quickCapture(QStringLiteral("activeScreen"));
        else if (arg == QLatin1String("cursor-screen"))
            quickCapture(QStringLiteral("cursorScreen"));
        else if (arg == QLatin1String("custom"))
            showCustomCaptureDialog();
        else if (arg == QLatin1String("whiteboard"))
            startWhiteboard();
        else {
            const QStringList parts = arg.split(QLatin1Char(','));
            if (parts.size() == 4)
                captureRegion(QRect(parts[0].toInt(), parts[1].toInt(), parts[2].toInt(), parts[3].toInt()), delay, false);
        }
    }
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

void App::onSizeInputRequested()
{
    // CAP-17：输入宽高
    bool ok = false;
    const QRect sel = m_session->selection();
    const QString text = QInputDialog::getText(nullptr, tr("Selection size"), tr("Width x Height (pixels)"), QLineEdit::Normal,
                                               QStringLiteral("%1x%2").arg(sel.width()).arg(sel.height()), &ok);
    if (!ok)
        return;
    const QStringList parts = text.split(QRegularExpression(QStringLiteral("[xX*,\\s]+")), Qt::SkipEmptyParts);
    if (parts.size() == 2)
        m_session->setSelectionSize(parts[0].toInt(), parts[1].toInt());
}

void App::onOcrCopyRequested(const QImage& image)
{
    if (!m_platform.ocr)
        return;
    const OcrResult r = m_platform.ocr->recognize(image);
    if (r.ok && !r.lines.isEmpty())
        QApplication::clipboard()->setText(r.text());
}

bool App::quickCapture(const QString& kind)
{
    if (m_session->isActive())
        return false;
    QRect region;
    const QList<ScreenInfo> screens = m_session->screenCapture() ? m_session->screenCapture()->screens() : QList<ScreenInfo>{};
    auto screenContaining = [&](QPoint p) {
        for (const auto& s : screens)
            if (s.physicalGeometry.contains(p))
                return s.physicalGeometry;
        return screens.isEmpty() ? QRect() : screens.first().physicalGeometry;
    };
    if (kind == QLatin1String("full")) {
        for (const auto& s : screens)
            region = region.united(s.physicalGeometry);
    } else if (kind == QLatin1String("last")) {
        const QStringList parts = m_settings.stringValue(QStringLiteral("capture.lastRegion")).split(QLatin1Char(','));
        if (parts.size() == 4)
            region = QRect(parts[0].toInt(), parts[1].toInt(), parts[2].toInt(), parts[3].toInt());
    } else if (kind == QLatin1String("activeWindow")) {
        if (m_platform.windowDetector)
            if (auto w = m_platform.windowDetector->activeWindow())
                region = w->frame;
    } else if (kind == QLatin1String("activeScreen")) {
        if (m_platform.windowDetector)
            if (auto w = m_platform.windowDetector->activeWindow())
                region = screenContaining(w->frame.center());
    } else if (kind == QLatin1String("cursorScreen")) {
        region = screenContaining(cursorPhysical());
    }
    if (region.isEmpty())
        return false;
    captureRegion(region, 0, false);
    return true;
}

void App::captureRegion(const QRect& physical, int delayMs, bool interactive)
{
    auto run = [this, physical, interactive] {
        if (!m_session->start(monotonicNs(), interactive))
            return;
        const QRect region = physical.intersected(m_session->desktop().bounds());
        if (region.isEmpty()) {
            m_session->cancel();
            return;
        }
        m_session->setSelection(region);
        if (!interactive)
            m_session->accept(); // 直接按 Enter 动作输出（CAP-04 / CAP-06）
    };
    if (delayMs > 0)
        QTimer::singleShot(delayMs, this, run);
    else
        run();
}

void App::showCustomCaptureDialog()
{
    CustomCaptureDialog dialog(m_settings);
    const QStringList parts = m_settings.stringValue(QStringLiteral("capture.lastRegion")).split(QLatin1Char(','));
    if (parts.size() == 4)
        dialog.setRegion(QRect(parts[0].toInt(), parts[1].toInt(), parts[2].toInt(), parts[3].toInt()));
    if (dialog.exec() != QDialog::Accepted)
        return;
    const auto r = dialog.request();
    captureRegion(r.region, r.delayMs, true);
}

void App::startWhiteboard()
{
    m_session->startWhiteboard(QColor::fromString(m_settings.stringValue(QStringLiteral("capture.whiteboardColor"))),
                               m_settings.boolValue(QStringLiteral("capture.whiteboardCurrentScreen")));
}

void App::startCapture(qint64 triggerNs)
{
    if (m_scroll && m_scroll->state() == ScrollCaptureSession::State::Running)
        return; // 滚动截图进行中不叠加截图
    m_session->start(triggerNs);
}

bool App::startScroll(const QRect& physicalRegion, bool showUi)
{
    if (m_scroll)
        m_scroll->cancel();
    m_scroll = std::make_unique<ScrollCaptureSession>(m_session->screenCapture(), m_platform.input.get(), m_settings);
    connect(m_scroll.get(), &ScrollCaptureSession::finished, this, [this](const QImage& image, const QRect& region) {
        m_scrollResult = image;
        m_scrollRegion = region;
    });
    connect(m_scroll.get(), &ScrollCaptureSession::outputRequested, this, [this](const QString& kind) {
        // CAP-73：完成后复制 / 保存 / 快速保存 / 贴图（贴图上可继续标注）
        if (m_scrollResult.isNull())
            return;
        if (kind == QLatin1String("copy"))
            m_output->copy(m_scrollResult);
        else if (kind == QLatin1String("save"))
            m_output->saveWithDialog(m_scrollResult, nullptr);
        else if (kind == QLatin1String("quickSave"))
            m_output->quickSave(m_scrollResult);
        else if (kind == QLatin1String("pin"))
            pinImage(m_scrollResult, m_scrollRegion.topLeft());
        m_history.add({m_scrollResult, m_scrollRegion, QDateTime::currentDateTime()});
        m_scroll->cancel(); // 关闭面板
    });
    connect(m_scroll.get(), &ScrollCaptureSession::cancelled, this, [this] { QMetaObject::invokeMethod(this, [this] { m_scroll.reset(); }, Qt::QueuedConnection); });
    if (!m_scroll->begin(physicalRegion, showUi)) {
        m_scrollError = m_scroll->lastError();
        if (m_tray)
            m_tray->showMessage(tr("Scrolling capture"), m_scrollError, QSystemTrayIcon::Warning);
        m_scroll.reset();
        return false;
    }
    m_scrollError.clear();
    return true;
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
