#include "app/App.h"

#include "app/CaptureSession.h"
#include "app/ActionsDialog.h"
#include "app/CommandServer.h"
#include "app/FloatingBar.h"
#include "app/GestureOverlay.h"
#include "app/GroupDialog.h"
#include "core/script/ScriptEngine.h"
#include "core/ocr/OcrFormat.h"
#include "core/pin/TextPin.h"
#include "app/Notification.h"
#include "app/ResultDialogs.h"
#include "app/Theme.h"
#include "app/CustomCaptureDialog.h"
#include "app/OutputActions.h"
#include "app/PinManager.h"
#include "app/PinWindow.h"
#include "app/PreferencesDialog.h"
#include "app/ScrollCaptureSession.h"
#include "core/config/Settings.h"
#include "core/util/Clock.h"

#include <QApplication>
#include <algorithm>
#include <QClipboard>
#include <QCursor>
#include <QDir>
#include <QRegularExpression>
#include <QTimer>
#include <QGuiApplication>
#include <QInputDialog>
#include <QMenu>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QProcess>
#include <QUrl>
#include <QPixmap>
#include <QScreen>
#include <QSystemTrayIcon>

#if defined(Q_OS_WIN)
#include "platform/windows/WinGlobalHotkey.h"
#include "platform/windows/WinInputHooks.h"
#include "platform/windows/WinSystem.h"
#include "platform/windows/WinWindowTools.h"
#elif defined(Q_OS_MACOS)
#include "platform/macos/MacPermissions.h"
#include "platform/macos/MacWindowTools.h"
#include <QPushButton>
#endif

namespace ss {

namespace {

QIcon makeTrayIcon(const QColor& color)
{
    QPixmap pm(64, 64);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(color, 6));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(8, 8, 48, 48), 8, 8);
    p.setBrush(color);
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
    connect(m_session.get(), &CaptureSession::pinRequested, this, [this](const QImage& image, const QRect& region) {
        pinImage(image, m_pins->resolvePosition(region.topLeft(), image.size(), true)); // PIN-13
    });
    connect(m_output.get(), &OutputActions::done, this, [this](OutputActions::Kind kind, const QString& path) {
        if (path.isEmpty() || (kind != OutputActions::Kind::Save && kind != OutputActions::Kind::QuickSave && kind != OutputActions::Kind::AutoSave))
            return;
        notify(kind == OutputActions::Kind::AutoSave ? tr("Auto-saved") : tr("Saved"), QDir::toNativeSeparators(path), path); // GEN-07
    });
    connect(m_session.get(), &CaptureSession::scrollRequested, this, [this](const QRect& region) { startScroll(region); });
    connect(m_session.get(), &CaptureSession::sizeInputRequested, this, &App::onSizeInputRequested);
    connect(m_session.get(), &CaptureSession::ocrCopyRequested, this, &App::onOcrCopyRequested);
    connect(m_session.get(), &CaptureSession::codesRequested, this, &App::onCodesRequested);
    connect(m_pins.get(), &PinManager::ocrRequested, this, &App::onOcrCopyRequested);
    connect(m_pins.get(), &PinManager::ocrLinesRequested, this, [this](PinWindow* pin, const QImage& image) {
        const OcrResult r = recognizeLines(image); // REC-04：图像贴图上的可选文字
        if (r.ok)
            pin->setOcrLines(r.lines);
    });
    applyOcrOptions();
    connect(m_pins.get(), &PinManager::codesRequested, this, [this](const QImage& image) {
        const QList<CodeResult> codes = decodeCodes(image);
        if (codes.isEmpty())
            QMessageBox::information(nullptr, tr("Decode codes"), tr("No QR code or barcode found."));
        else
            showCodeResult(codes);
    });
    theme::apply(m_settings);
    applyFloatingBar();
    applyInputHooks();
    connect(&m_hotCornerTimer, &QTimer::timeout, this, [this] { pollHotCorners(cursorPhysical(), QDateTime::currentMSecsSinceEpoch()); });
    m_hotCornerTimer.start(150);
#ifdef Q_OS_WIN
    win::setProcessPriority(m_settings.stringValue(QStringLiteral("general.processPriority")));
#endif
    loadHistoryDir();
    if (m_platform.screenCapture)
        m_platform.screenCapture->configure(QStringLiteral("hdrStrategy"), m_settings.stringValue(QStringLiteral("capture.hdrStrategy")));
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
    m_trayMenu->addAction(tr("Pin files selected in Explorer"), this, [this] { m_pins->pinExplorerSelection(cursorPhysical()); });
    QMenu* all = m_trayMenu->addMenu(tr("All pins"));
    all->addAction(tr("Thumbnails"), this, [this] { m_pins->setAllThumbnails(true); });
    all->addAction(tr("Full size"), this, [this] { m_pins->setAllThumbnails(false); });
    all->addAction(tr("Save all to folder..."), this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(nullptr, tr("Save all pins"));
        if (!dir.isEmpty())
            m_pins->saveAllToFolder(dir);
    });
    all->addAction(tr("Solo selected pins"), this, [this](bool on) { m_pins->setSolo(on); })->setCheckable(true);
    all->addAction(tr("Close all"), this, [this] { m_pins->closeAll(); });
    all->addAction(tr("Destroy all"), this, [this] { m_pins->destroyAll(); });
    m_actionsMenu = m_trayMenu->addMenu(tr("Actions"));
    rebuildActionsMenu();
    QMenu* recognize = m_trayMenu->addMenu(tr("Recognize"));
    recognize->addAction(tr("Text in image file (OCR)..."), this, [this] { recognizeTextFromFile(); });
    recognize->addAction(tr("QR code / barcode in image file..."), this, [this] { decodeCodesFromFile(); });
    m_groupMenu = m_trayMenu->addMenu(tr("Pin groups"));
    connect(m_pins.get(), &PinManager::groupChanged, this, [this] { rebuildGroupMenu(); });
    rebuildGroupMenu();
    m_trayMenu->addSeparator();
    m_hotkeysAction = m_trayMenu->addAction(tr("Disable hotkeys"), this, [this](bool checked) { setHotkeysEnabled(!checked); });
    m_hotkeysAction->setCheckable(true);
    m_hotkeysAction->setChecked(!hotkeysEnabled());
    m_trayMenu->addAction(tr("Preferences..."), this, &App::showPreferences);
    QMenu* configMenu = m_trayMenu->addMenu(tr("Settings file"));
    configMenu->addAction(tr("Export settings..."), this, [this] {
        const QString path = QFileDialog::getSaveFileName(nullptr, tr("Export settings"), QStringLiteral("screenshot-settings.json"), tr("Settings (*.json)"));
        if (!path.isEmpty())
            m_settings.exportTo(path);
    });
    configMenu->addAction(tr("Import settings..."), this, [this] {
        const QString path = QFileDialog::getOpenFileName(nullptr, tr("Import settings"), QString(), tr("Settings (*.json)"));
        if (!path.isEmpty() && m_settings.importFrom(path))
            m_settings.saveToFile();
    });
    configMenu->addAction(tr("Open settings folder"), this, &App::openSettingsFolder);
    m_trayMenu->addAction(tr("Help"), this, &App::showAbout);
    m_trayMenu->addSeparator();
    m_trayMenu->addAction(tr("Quit"), this, &App::quit);
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;
    m_tray = new QSystemTrayIcon(makeTrayIcon(trayIconColor()), this);
    m_tray->setToolTip(QStringLiteral("ScreenShot"));
    m_tray->setContextMenu(m_trayMenu);
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) { trayActivated(reason); });
    m_tray->setVisible(!m_settings.boolValue(QStringLiteral("general.trayIconHidden")));
}

QColor App::trayIconColor() const
{
    // GEN-06：跟随系统（深色托盘用浅色图标）/ 浅色 / 深色 / 自定义
    const QString style = m_settings.stringValue(QStringLiteral("general.trayIconStyle"));
    if (style == QLatin1String("custom"))
        return QColor::fromString(m_settings.stringValue(QStringLiteral("general.trayIconColor")));
    if (style == QLatin1String("light"))
        return QColor(245, 245, 245);
    if (style == QLatin1String("dark"))
        return QColor(40, 40, 40);
    return theme::resolve(m_settings) == QLatin1String("dark") ? QColor(245, 245, 245) : QColor(40, 120, 220);
}

void App::applyTrayAppearance()
{
    if (!m_tray)
        return;
    m_tray->setIcon(makeTrayIcon(trayIconColor()));
    m_tray->setVisible(!m_settings.boolValue(QStringLiteral("general.trayIconHidden")));
}

void App::applyFloatingBar()
{
    const bool on = m_settings.boolValue(QStringLiteral("general.floatingBar"));
    if (!on) {
        m_floatingBar.reset();
        return;
    }
    if (m_floatingBar)
        return;
    m_floatingBar = std::make_unique<FloatingBar>();
    connect(m_floatingBar.get(), &FloatingBar::captureRequested, this, [this] { startCapture(monotonicNs()); });
    connect(m_floatingBar.get(), &FloatingBar::pinRequested, this, &App::pinFromClipboard);
    connect(m_floatingBar.get(), &FloatingBar::dropped, this, [this](const QMimeData* mime, QPoint at) { m_pins->pinMime(mime, at); });
    connect(m_floatingBar.get(), &FloatingBar::moved, this, [this](QPoint pos) {
        m_settings.setValue(QStringLiteral("general.floatingBarPos"), QStringLiteral("%1,%2").arg(pos.x()).arg(pos.y()));
    });
    const QStringList pos = m_settings.stringValue(QStringLiteral("general.floatingBarPos")).split(QLatin1Char(','));
    if (pos.size() == 2)
        m_floatingBar->move(pos[0].toInt(), pos[1].toInt());
    else if (QScreen* s = QGuiApplication::primaryScreen())
        m_floatingBar->move(s->availableGeometry().right() - m_floatingBar->width() - 20, s->availableGeometry().top() + 20);
    m_floatingBar->show();
}

void App::rebuildGroupMenu()
{
    if (!m_groupMenu)
        return;
    m_groupMenu->clear();
    m_groupMenu->addAction(tr("New group..."), this, [this] {
        bool ok = false;
        const QString name = QInputDialog::getText(nullptr, tr("New pin group"), tr("Group name:"), QLineEdit::Normal, QString(), &ok);
        if (ok)
            m_pins->newGroup(name);
        rebuildGroupMenu();
    });
    m_groupMenu->addAction(tr("Next group\tCtrl+Shift+3"), this, [this] { m_pins->nextGroup(); });
    m_groupMenu->addAction(tr("Previous group"), this, [this] { m_pins->previousGroup(); });
    m_groupMenu->addAction(tr("Clear this group"), this, [this] { m_pins->clearCurrentGroup(); });
    m_groupMenu->addAction(tr("Manage groups..."), this, [this] {
        GroupDialog dialog(*m_pins);
        dialog.exec();
        rebuildGroupMenu();
    });
    m_groupMenu->addSeparator();
    const QStringList names = m_pins->groupNames();
    for (int i = 0; i < names.size(); ++i) {
        const auto [onScreen, total] = m_pins->groupCounts(i); // GRP-03：屏幕上 / 总数
        QAction* a = m_groupMenu->addAction(QStringLiteral("%1  (%2 / %3)").arg(names[i]).arg(onScreen).arg(total), this, [this, i] { m_pins->switchToGroup(i); });
        a->setCheckable(true);
        a->setChecked(i == m_pins->currentGroupIndex());
    }
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
        if (hotkeyIgnoredForForeground())
            return; // ACT-07
        if (m_settings.boolValue(QStringLiteral("hotkey.ignoreInjected")) && m_hotkeyVk.contains(id) && m_injected.shouldIgnore(m_hotkeyVk.value(id), QDateTime::currentMSecsSinceEpoch()))
            return; // ACT-15：由模拟按键触发的热键
        switch (id) {
        case HotkeyCapture: startCapture(atNs); break;
        case HotkeyPin: pinFromClipboard(); break;
        case HotkeyTogglePins: togglePinsHidden(); break;
        case HotkeyRestorePin: restoreClosedPin(); break;
        case HotkeyPassthrough: togglePassthroughAtCursor(); break;
        case HotkeyNextGroup: m_pins->nextGroup(); break;
        case HotkeyPreviousGroup: m_pins->previousGroup(); break;
        case HotkeyToggleWindowTopmost:
#ifdef Q_OS_WIN
            win::toggleWindowTopmostAt(cursorPhysical()); // ACT-14
#endif
            break;
        case HotkeyPasteAsImage: pasteAs(QStringLiteral("image")); break;
        case HotkeyPasteAsText: pasteAs(QStringLiteral("text")); break;
        default:
            if (id >= HotkeyActionBase) {
                const QList<Action> list = actions();
                if (id - HotkeyActionBase < list.size())
                    runAction(list[id - HotkeyActionBase].name);
            }
            break;
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
    for (int id = HotkeyActionBase; id < HotkeyActionBase + m_actionHotkeys; ++id)
        m_platform.hotkey->unregisterHotkey(id);
    m_actionHotkeys = 0;
    for (int id : {HotkeyCapture, HotkeyPin, HotkeyTogglePins, HotkeyRestorePin, HotkeyPassthrough, HotkeyNextGroup, HotkeyPreviousGroup, HotkeyToggleWindowTopmost, HotkeyPasteAsImage, HotkeyPasteAsText})
        m_platform.hotkey->unregisterHotkey(id);
    m_hotkeyStatus.clear();
    if (!hotkeysEnabled())
        return;
    const QList<QPair<int, QString>> keys{{HotkeyCapture, QStringLiteral("hotkey.capture")}, {HotkeyPin, QStringLiteral("hotkey.pin")},
                                          {HotkeyTogglePins, QStringLiteral("hotkey.togglePins")}, {HotkeyRestorePin, QStringLiteral("hotkey.restorePin")},
                                          {HotkeyPassthrough, QStringLiteral("hotkey.togglePassthrough")}, {HotkeyNextGroup, QStringLiteral("hotkey.nextGroup")},
                                          {HotkeyPreviousGroup, QStringLiteral("hotkey.previousGroup")}, {HotkeyToggleWindowTopmost, QStringLiteral("hotkey.toggleWindowTopmost")},
                                          {HotkeyPasteAsImage, QStringLiteral("hotkey.pasteAsImage")}, {HotkeyPasteAsText, QStringLiteral("hotkey.pasteAsText")}};
    for (const auto& [id, settingKey] : keys) {
        const QKeySequence seq(m_settings.stringValue(settingKey));
        const bool ok = !seq.isEmpty() && hotkeyAllowed(seq) && m_platform.hotkey->registerHotkey(id, seq);
#ifdef Q_OS_WIN
        unsigned mods = 0, vk = 0;
        if (ok && win::WinGlobalHotkey::toNative(seq, mods, vk))
            m_hotkeyVk[id] = static_cast<int>(vk);
#endif
        m_hotkeyStatus[id] = ok;
        if (!ok && !seq.isEmpty()) {
            qWarning("hotkey %s (%s) could not be registered", qPrintable(settingKey), qPrintable(seq.toString()));
            if (m_tray)
                m_tray->showMessage(tr("Hotkey conflict"), tr("%1 is already used by another program.").arg(seq.toString(QKeySequence::NativeText)), QSystemTrayIcon::Warning);
        }
    }
    // 动作热键（ACT-03）
    const QList<Action> list = actions();
    for (int i = 0; i < list.size(); ++i) {
        const QKeySequence seq(list[i].hotkey);
        if (!seq.isEmpty())
            m_platform.hotkey->registerHotkey(HotkeyActionBase + i, seq);
    }
    m_actionHotkeys = static_cast<int>(list.size());
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
    } else if (key == QStringLiteral("general.autostart") || key == QStringLiteral("general.runAsAdmin")) {
#ifdef Q_OS_WIN
        // 管理员模式下自启用计划任务（GEN-05），否则用注册表
        const bool admin = m_settings.boolValue(QStringLiteral("general.runAsAdmin"));
        const bool on = m_settings.boolValue(QStringLiteral("general.autostart"));
        win::setAutostart(on && !admin, QCoreApplication::applicationFilePath(), QStringLiteral("ScreenShot"));
        win::setAutostartElevated(on && admin, QCoreApplication::applicationFilePath(), QStringLiteral("ScreenShot"));
#endif
    } else if (key == QStringLiteral("actions.list")) {
        registerHotkeys();
        rebuildActionsMenu();
    } else if (key.startsWith(QStringLiteral("ui."))) {
        theme::apply(m_settings);
        applyTrayAppearance();
    } else if (key.startsWith(QStringLiteral("general.trayIcon"))) {
        applyTrayAppearance();
    } else if (key == QStringLiteral("general.floatingBar")) {
        applyFloatingBar();
    } else if (key.startsWith(QStringLiteral("globalMouse.")) || key.startsWith(QStringLiteral("hotkey.doubleTap")) || key == QStringLiteral("hotkey.ignoreInjected") || key.startsWith(QStringLiteral("hotCorner."))) {
        applyInputHooks();
    } else if (key == QStringLiteral("general.processPriority")) {
#ifdef Q_OS_WIN
        win::setProcessPriority(m_settings.stringValue(key)); // GEN-10
#endif
    } else if (key.startsWith(QStringLiteral("ocr."))) {
        applyOcrOptions();
    } else if (key == QStringLiteral("capture.hdrStrategy")) {
        if (m_platform.screenCapture)
            m_platform.screenCapture->configure(QStringLiteral("hdrStrategy"), m_settings.stringValue(key));
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
    connect(m_commands.get(), &CommandServer::commandReceived, this, [this](const QStringList& command, int id) { runCommand(command, id); });
    m_commands->start();
}

namespace {

// 取选项的值：--name value；不存在返回空
QString optionValue(const QStringList& command, const QString& name)
{
    const int i = static_cast<int>(command.indexOf(name));
    return i >= 0 ? command.value(i + 1) : QString();
}

// 取选项后直到下一个 -- 选项为止的全部参数（文本可含多个词）
QString optionText(const QStringList& command, const QString& name)
{
    const int i = static_cast<int>(command.indexOf(name));
    if (i < 0)
        return {};
    QStringList words;
    for (int j = i + 1; j < command.size() && !command[j].startsWith(QStringLiteral("--")); ++j)
        words.append(command[j]);
    return words.join(QLatin1Char(' '));
}

QPoint parsePoint(const QString& text, QPoint fallback)
{
    const QStringList parts = text.split(QLatin1Char(','));
    return parts.size() == 2 ? QPoint(parts[0].toInt(), parts[1].toInt()) : fallback;
}

} // namespace

void App::finishCommand(int requestId, const QString& text)
{
    if (requestId && m_commands)
        m_commands->reply(requestId, text);
}

QString App::runCommand(const QStringList& command, int requestId)
{
    const QString name = command.value(0);
    const bool wait = command.contains(QStringLiteral("--wait"));
    QString result = QStringLiteral("ok");
    if (name == QLatin1String("snip")) {
        // snip [full|last|active-window|active-screen|cursor-screen|custom|whiteboard|x,y,w,h]
        //      [--size WxH [--pos x,y]] [--delay ms] [--shadow] [--output copy,pin,save,quickSave,ocrCopy] [--edit] [--wait]
        const QString arg = command.value(1);
        const int delay = optionValue(command, QStringLiteral("--delay")).toInt();
        const bool edit = command.contains(QStringLiteral("--edit"));
        const QString output = optionValue(command, QStringLiteral("--output"));
        if (!output.isEmpty())
            m_session->setEnterActionsOverride(output.split(QLatin1Char(','), Qt::SkipEmptyParts));
        if (command.contains(QStringLiteral("--shadow")))
            m_session->setShadowEnabled(true);
        if (wait) {
            // 完成（输出或取消）后再应答调用方
            m_pendingReply = requestId;
            auto done = [this](const QString& text) {
                if (m_pendingReply) {
                    finishCommand(m_pendingReply, text);
                    m_pendingReply = 0;
                }
            };
            connect(m_session.get(), &CaptureSession::captured, this, [done] { done(QStringLiteral("ok")); }, Qt::SingleShotConnection);
            connect(m_session.get(), &CaptureSession::cancelled, this, [done] { done(QStringLiteral("cancelled")); }, Qt::SingleShotConnection);
        }
        QRect region;
        const QString size = optionValue(command, QStringLiteral("--size"));
        if (!size.isEmpty()) {
            const QStringList wh = size.split(QRegularExpression(QStringLiteral("[xX*]")));
            if (wh.size() == 2)
                region = QRect(parsePoint(optionValue(command, QStringLiteral("--pos")), cursorPhysical()), QSize(wh[0].toInt(), wh[1].toInt()));
        }
        if (!region.isEmpty())
            captureRegion(region, delay, edit);
        else if (arg.isEmpty() || arg.startsWith(QStringLiteral("--")))
            startCapture(monotonicNs());
        else if (arg == QLatin1String("full") || arg == QLatin1String("last"))
            edit ? captureRegion(quickRegion(arg), delay, true) : (void)quickCapture(arg);
        else if (arg == QLatin1String("active-window"))
            edit ? captureRegion(quickRegion(QStringLiteral("activeWindow")), delay, true) : (void)quickCapture(QStringLiteral("activeWindow"));
        else if (arg == QLatin1String("active-screen"))
            edit ? captureRegion(quickRegion(QStringLiteral("activeScreen")), delay, true) : (void)quickCapture(QStringLiteral("activeScreen"));
        else if (arg == QLatin1String("cursor-screen"))
            edit ? captureRegion(quickRegion(QStringLiteral("cursorScreen")), delay, true) : (void)quickCapture(QStringLiteral("cursorScreen"));
        else if (arg == QLatin1String("custom"))
            showCustomCaptureDialog();
        else if (arg == QLatin1String("whiteboard"))
            startWhiteboard();
        else {
            const QStringList parts = arg.split(QLatin1Char(','));
            if (parts.size() == 4)
                captureRegion(QRect(parts[0].toInt(), parts[1].toInt(), parts[2].toInt(), parts[3].toInt()), delay, edit);
        }
        if (wait)
            return result; // 稍后应答
    } else if (name == QLatin1String("pin")) {
        // pin [clipboard] [--text words...] [--html words...] [--file path...] [--pos x,y]
        const QPoint at = parsePoint(optionValue(command, QStringLiteral("--pos")), cursorPhysical());
        PinWindow* pin = nullptr;
        if (command.contains(QStringLiteral("--text")))
            pin = m_pins->pinText(optionText(command, QStringLiteral("--text")), at);
        else if (command.contains(QStringLiteral("--html")))
            pin = m_pins->pinHtml(optionText(command, QStringLiteral("--html")), at);
        else if (command.contains(QStringLiteral("--file"))) {
            QMimeData mime;
            QList<QUrl> urls;
            for (const QString& f : optionText(command, QStringLiteral("--file")).split(QLatin1Char(' '), Qt::SkipEmptyParts))
                urls.append(QUrl::fromLocalFile(f));
            mime.setUrls(urls);
            pin = m_pins->pinMime(&mime, at);
        } else {
            pin = m_pins->pinHotkey(at);
        }
        result = pin ? QStringLiteral("ok") : QStringLiteral("nothing to pin");
    } else if (name == QLatin1String("pins")) {
        // pins hide|show|toggle|close-all|destroy-all|restore|restore-offscreen
        const QString op = command.value(1);
        if (op == QLatin1String("hide")) m_pins->hideAll();
        else if (op == QLatin1String("show")) m_pins->showAll();
        else if (op == QLatin1String("toggle")) togglePinsHidden();
        else if (op == QLatin1String("close-all")) m_pins->closeAll();
        else if (op == QLatin1String("destroy-all")) m_pins->destroyAll();
        else if (op == QLatin1String("restore")) restoreClosedPin();
        else if (op == QLatin1String("restore-offscreen")) restoreOffscreenPins();
        else if (op == QLatin1String("thumbnails")) m_pins->setAllThumbnails(true);
        else if (op == QLatin1String("full-size")) m_pins->setAllThumbnails(false);
        else if (op == QLatin1String("save-all")) result = QString::number(m_pins->saveAllToFolder(command.mid(2).join(QLatin1Char(' '))));
        else if (op == QLatin1String("explorer")) result = m_pins->pinExplorerSelection(cursorPhysical()) ? QStringLiteral("ok") : QStringLiteral("nothing selected");
        else result = QStringLiteral("unknown pins command");
    } else if (name == QLatin1String("group")) {
        // group new NAME | switch NAME | next | prev | clear | list
        const QString op = command.value(1);
        if (op == QLatin1String("new")) m_pins->newGroup(optionText(command, op).isEmpty() ? command.mid(2).join(QLatin1Char(' ')) : command.mid(2).join(QLatin1Char(' ')));
        else if (op == QLatin1String("switch")) result = m_pins->switchToGroupNamed(command.mid(2).join(QLatin1Char(' '))) ? QStringLiteral("ok") : QStringLiteral("no such group");
        else if (op == QLatin1String("next")) m_pins->nextGroup();
        else if (op == QLatin1String("prev")) m_pins->previousGroup();
        else if (op == QLatin1String("clear")) m_pins->clearCurrentGroup();
        else if (op == QLatin1String("list")) result = m_pins->groupNames().join(QLatin1Char('\n'));
        else result = QStringLiteral("unknown group command");
        rebuildGroupMenu();
    } else if (name == QLatin1String("action")) {
        result = runAction(command.mid(1).join(QLatin1Char(' '))) ? QStringLiteral("ok") : QStringLiteral("no such action");
    } else if (name == QLatin1String("ocr-file")) {
        recognizeTextFromFile(command.value(1));
    } else if (name == QLatin1String("qr-file")) {
        decodeCodesFromFile(command.value(1));
    } else if (name == QLatin1String("toggle-pins")) {
        togglePinsHidden();
    } else if (name == QLatin1String("prefs")) {
        showPreferences();
    } else if (name == QLatin1String("prefs-dir")) {
        openSettingsFolder();
    } else if (name == QLatin1String("script")) {
        bool ok = false;
        result = runScript(command.mid(1).join(QLatin1Char(' ')), &ok); // ACT-13
        if (!ok)
            result = QStringLiteral("script error: ") + result;
    } else if (name == QLatin1String("script-file")) {
        QFile f(command.value(1));
        if (!f.open(QIODevice::ReadOnly))
            result = QStringLiteral("cannot read script");
        else
            result = runScript(QString::fromUtf8(f.readAll()));
    } else if (name == QLatin1String("paste-as")) {
        pasteAs(command.value(1));
    } else if (name == QLatin1String("quit")) {
        quit();
    } else if (!name.isEmpty()) {
        qWarning("unknown command: %s", qPrintable(name));
        result = QStringLiteral("unknown command");
    }
    finishCommand(requestId, result);
    return result;
}

QString App::runScript(const QString& source, bool* ok)
{
    ScriptHost host;
    host.command = [this](const QString& line) { return runCommand(splitCommandLine(line)); };
    host.getClipboard = [] { return QApplication::clipboard()->text(); };
    host.setClipboard = [](const QString& t) { QApplication::clipboard()->setText(t); };
    host.sleep = [](int ms) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms)
            QApplication::processEvents(QEventLoop::AllEvents, 20);
    };
    ScriptEngine engine(host);
    const ScriptResult r = engine.run(source);
    if (ok)
        *ok = r.ok;
    if (!r.ok)
        qWarning("script error: %s", qPrintable(r.error));
    return r.ok ? (r.output.isEmpty() ? r.value : r.output) : r.error;
}

// ---- 动作（ACT-03）----

QList<Action> App::actions() const
{
    return actionsFromJson(m_settings.stringValue(QStringLiteral("actions.list")));
}

bool App::runAction(const QString& name)
{
    for (const Action& a : actions()) {
        if (a.name.compare(name, Qt::CaseInsensitive) != 0)
            continue;
        const QStringList words = splitCommandLine(a.command);
        if (words.value(0) == QLatin1String("shell")) {
            QProcess::startDetached(QStringLiteral("cmd.exe"), {QStringLiteral("/c"), a.command.mid(6).trimmed()});
        } else if (words.value(0) == QLatin1String("run")) {
            QProcess::startDetached(words.value(1), words.mid(2));
        } else if (words.value(0) == QLatin1String("script")) {
            runScript(a.command.mid(7)); // ACT-13：动作绑定脚本文本
        } else if (words.value(0) == QLatin1String("script-file")) {
            QFile f(words.value(1));
            if (f.open(QIODevice::ReadOnly))
                runScript(QString::fromUtf8(f.readAll()));
        } else {
            runCommand(words);
        }
        return true;
    }
    return false;
}

void App::showActionsDialog()
{
    ActionsDialog dialog(m_settings);
    dialog.exec();
}

void App::rebuildActionsMenu()
{
    if (!m_actionsMenu)
        return;
    m_actionsMenu->clear();
    m_actionsMenu->addAction(tr("Edit actions..."), this, &App::showActionsDialog);
    m_actionsMenu->addSeparator();
    for (const Action& a : actions()) {
        if (!a.inTray)
            continue;
        const QString n = a.name;
        QAction* act = m_actionsMenu->addAction(a.hotkey.isEmpty() ? a.name : a.name + QLatin1Char('\t') + a.hotkey, this, [this, n] { runAction(n); });
        Q_UNUSED(act);
    }
}

// ---- 识别（REC-01、REC-06）----

void App::showOcrResult(const QString& text, const QList<OcrLine>& lines)
{
    QApplication::clipboard()->setText(text);
    if (!m_settings.boolValue(QStringLiteral("ocr.showResult")))
        return;
    auto* dialog = new OcrResultDialog(text);
    dialog->setLines(lines);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void App::showCodeResult(const QList<CodeResult>& codes)
{
    auto* dialog = new CodeResultDialog(codes);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void App::onCodesRequested(const QList<CodeResult>& codes)
{
    showCodeResult(codes);
}

void App::recognizeTextFromFile(const QString& pathIn)
{
    const QString path = pathIn.isEmpty() ? QFileDialog::getOpenFileName(nullptr, tr("Recognize text in image"), QString(), tr("Images (*.png *.jpg *.jpeg *.bmp *.webp)")) : pathIn;
    if (path.isEmpty() || !m_platform.ocr)
        return;
    const OcrResult r = m_platform.ocr->recognize(QImage(path));
    showOcrResult(r.ok ? formatOcr(r) : r.error, r.lines);
}

void App::decodeCodesFromFile(const QString& pathIn)
{
    const QString path = pathIn.isEmpty() ? QFileDialog::getOpenFileName(nullptr, tr("Decode codes in image"), QString(), tr("Images (*.png *.jpg *.jpeg *.bmp *.webp)")) : pathIn;
    if (path.isEmpty())
        return;
    const QList<CodeResult> codes = decodeCodes(QImage(path));
    if (codes.isEmpty())
        QMessageBox::information(nullptr, tr("Decode codes in image"), tr("No QR code or barcode found."));
    else
        showCodeResult(codes);
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

void App::applyOcrOptions()
{
    if (!m_platform.ocr)
        return;
    m_platform.ocr->configure(QStringLiteral("language"), m_settings.stringValue(QStringLiteral("ocr.language")));
    m_platform.ocr->configure(QStringLiteral("mode"), m_settings.stringValue(QStringLiteral("ocr.mode")));
}

QString App::formatOcr(const OcrResult& r) const
{
    return formatOcrText(r.lines, ocrLayoutFromName(m_settings.stringValue(QStringLiteral("ocr.layout"))), ocrPunctuationFromName(m_settings.stringValue(QStringLiteral("ocr.punctuation"))));
}

void App::onOcrCopyRequested(const QImage& image)
{
    if (!m_platform.ocr)
        return;
    const OcrResult r = m_platform.ocr->recognize(image);
    if (r.ok && !r.lines.isEmpty())
        showOcrResult(formatOcr(r), r.lines);
}

OcrResult App::recognizeLines(const QImage& image)
{
    return m_platform.ocr ? m_platform.ocr->recognize(image) : OcrResult{};
}

bool App::quickCapture(const QString& kind)
{
    if (m_session->isActive())
        return false;
    const QRect region = quickRegion(kind);
    if (region.isEmpty())
        return false;
    captureRegion(region, 0, false);
    return true;
}

QRect App::quickRegion(const QString& kind)
{
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
    return region;
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

bool App::hotkeyAllowed(const QKeySequence& seq) const
{
    // ACT-08：单键（无修饰键、非功能键）默认不允许作为全局热键
    if (seq.isEmpty() || m_settings.boolValue(QStringLiteral("hotkey.allowSingleKey")))
        return true;
    const QKeyCombination c = seq[0];
    const bool hasModifier = c.keyboardModifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier);
    const bool functionKey = c.key() >= Qt::Key_F1 && c.key() <= Qt::Key_F35;
    const bool special = c.key() == Qt::Key_Print || c.key() == Qt::Key_Pause || c.key() == Qt::Key_ScrollLock;
    return hasModifier || functionKey || special;
}

bool App::hotkeyIgnoredForForeground() const
{
    const QStringList names = m_settings.stringValue(QStringLiteral("hotkey.ignoreProcesses")).split(QLatin1Char(','), Qt::SkipEmptyParts);
    if (names.isEmpty() || !m_platform.windowDetector)
        return false;
    const auto active = m_platform.windowDetector->activeWindow();
    if (!active)
        return false;
    for (const QString& n : names)
        if (!n.trimmed().isEmpty() && active->processName.compare(n.trimmed(), Qt::CaseInsensitive) == 0)
            return true;
    return false;
}

void App::pasteAs(const QString& kind)
{
    // ACT-16：剪贴板转换后模拟 Ctrl+V
    QClipboard* cb = QApplication::clipboard();
    const QMimeData* mime = cb->mimeData();
    if (!mime)
        return;
    if (kind == QLatin1String("text")) {
        if (!mime->hasText())
            return;
        cb->setText(mime->text());
    } else {
        QImage image;
        if (mime->hasImage())
            image = qvariant_cast<QImage>(mime->imageData());
        else if (mime->hasText()) {
            TextPinOptions o;
            o.font = QFont(m_settings.stringValue(QStringLiteral("pin.textFont")), m_settings.intValue(QStringLiteral("pin.textSize")));
            image = renderTextPin(mime->text(), o);
        }
        if (image.isNull())
            return;
        cb->setImage(image);
    }
    m_pins->markClipboardDirty();
    if (m_platform.input)
        m_platform.input->sendKeyCombo(Qt::ControlModifier, Qt::Key_V);
}

void App::openSettingsFolder()
{
    // CFG-08：一键打开配置文件所在目录
    const QString path = m_settings.filePath().isEmpty() ? m_dataDir : m_settings.filePath();
    if (!path.isEmpty())
        NotificationWidget::revealHandler(path);
}

// ---- 全局输入（ACT-10 / 11 / 12 / 15）----

void App::applyInputHooks()
{
    m_doubleTap.setThreshold(m_settings.intValue(QStringLiteral("hotkey.doubleTapMs")));
    m_hotCorners = HotCornerDetector(3, m_settings.intValue(QStringLiteral("hotCorner.dwellMs")));
#ifdef Q_OS_WIN
    const bool needKeyboard = m_settings.stringValue(QStringLiteral("hotkey.doubleTap")) != QLatin1String("none") || m_settings.boolValue(QStringLiteral("hotkey.ignoreInjected"))
        || m_settings.boolValue(QStringLiteral("globalMouse.enabled"));
    const bool needMouse = m_settings.boolValue(QStringLiteral("globalMouse.enabled"));
    if (!needKeyboard && !needMouse) {
        m_hooks.reset();
        return;
    }
    if (!m_hooks) {
        m_hooks = std::make_unique<win::WinInputHooks>();
        connect(m_hooks.get(), &win::WinInputHooks::keyDown, this, &App::onGlobalKeyDown);
        connect(m_hooks.get(), &win::WinInputHooks::keyUp, this, [this](int vk, bool, qint64) {
            // 修饰键松开：全局鼠标手势的修饰状态
            static const int modifiers[] = {0x5B, 0x5C, 0x12, 0xA4, 0xA5, 0x11, 0xA2, 0xA3, 0x10, 0xA0, 0xA1}; // Win / Alt / Ctrl / Shift 的虚拟键
            if (std::find(std::begin(modifiers), std::end(modifiers), vk) != std::end(modifiers))
                m_gesture.setModifierHeld(false);
        });
        connect(m_hooks.get(), &win::WinInputHooks::mouseButton, this, &App::onGlobalMouse);
        connect(m_hooks.get(), &win::WinInputHooks::mouseMoved, this, &App::onGlobalMouseMoved);
    }
    if (needKeyboard)
        m_hooks->installKeyboard();
    if (needMouse)
        m_hooks->installMouse();
#endif
}

void App::runNamedAction(const QString& action)
{
    if (action == QLatin1String("capture"))
        startCapture(monotonicNs());
    else if (action == QLatin1String("pin"))
        pinFromClipboard();
    else if (action == QLatin1String("togglePins"))
        togglePinsHidden();
    else if (action == QLatin1String("restorePin"))
        restoreClosedPin();
}

void App::onGlobalKeyDown(int vk, bool injected, qint64 nowMs)
{
    m_injected.keyEvent(vk, injected, nowMs); // ACT-15
    // 全局鼠标手势的修饰键
    const QString mod = m_settings.stringValue(QStringLiteral("globalMouse.modifier"));
    auto isModifier = [](const QString& name, int key) {
        if (name == QLatin1String("win")) return key == 0x5B || key == 0x5C;
        if (name == QLatin1String("alt")) return key == 0x12 || key == 0xA4 || key == 0xA5;
        if (name == QLatin1String("ctrl")) return key == 0x11 || key == 0xA2 || key == 0xA3;
        if (name == QLatin1String("shift")) return key == 0x10 || key == 0xA0 || key == 0xA1;
        return false;
    };
    if (m_settings.boolValue(QStringLiteral("globalMouse.enabled")) && isModifier(mod, vk))
        m_gesture.setModifierHeld(true);
    // 修饰键双击（ACT-10）
    const QString tap = m_settings.stringValue(QStringLiteral("hotkey.doubleTap"));
    if (tap != QLatin1String("none") && !injected) {
        if (m_doubleTap.keyDown(isModifier(tap, vk), nowMs))
            runNamedAction(m_settings.stringValue(QStringLiteral("hotkey.doubleTapAction")));
    }
}

bool App::globalMouseGestureActive() const
{
    return m_gesture.active();
}

void App::onGlobalMouse(int button, bool down, QPoint physical)
{
    if (!m_settings.boolValue(QStringLiteral("globalMouse.enabled")))
        return;
    if (down) {
        if (m_gesture.press(button, physical)) {
#ifdef Q_OS_WIN
            if (m_hooks)
                m_hooks->setSwallowMouse(true);
#endif
            if (!m_gestureOverlay)
                m_gestureOverlay = std::make_unique<GestureOverlay>();
            m_gestureOverlay->showRegion(m_gesture.currentRegion());
        }
        return;
    }
    const auto result = m_gesture.release(button, physical);
    if (!m_gesture.active()) {
#ifdef Q_OS_WIN
        if (m_hooks)
            m_hooks->setSwallowMouse(false);
#endif
        if (m_gestureOverlay)
            m_gestureOverlay->hideRegion();
    }
    if (result)
        finishGlobalMouse(result->region, result->button);
}

void App::onGlobalMouseMoved(QPoint physical)
{
    if (m_gesture.move(physical) && m_gestureOverlay)
        m_gestureOverlay->showRegion(m_gesture.currentRegion());
}

void App::finishGlobalMouse(const QRect& region, int button)
{
    // ACT-11：按键决定输出
    const QString key = button == 1 ? QStringLiteral("globalMouse.left") : button == 2 ? QStringLiteral("globalMouse.middle") : QStringLiteral("globalMouse.right");
    const QString action = m_settings.stringValue(key);
    if (action == QLatin1String("none") || m_session->isActive())
        return;
    m_session->setEnterActionsOverride({action});
    captureRegion(region, 0, false);
}

void App::pollHotCorners(QPoint physical, qint64 nowMs)
{
    QRect screen;
    for (QScreen* s : QGuiApplication::screens()) {
        const QRect g = s->geometry();
        const QRect phys(g.topLeft(), QSize(qRound(g.width() * s->devicePixelRatio()), qRound(g.height() * s->devicePixelRatio())));
        if (phys.contains(physical))
            screen = phys;
    }
    if (screen.isNull())
        return;
    const auto corner = m_hotCorners.feed(physical, screen, nowMs);
    if (corner == HotCornerDetector::Corner::None)
        return;
    runNamedAction(m_settings.stringValue(QStringLiteral("hotCorner.") + HotCornerDetector::cornerName(corner))); // ACT-12
}

void App::loadHistoryDir()
{
    // CAP-43：从历史目录读回最近的截图
    const QString dir = m_settings.stringValue(QStringLiteral("capture.historyDir"));
    if (dir.isEmpty())
        return;
    QStringList files = QDir(dir).entryList({QStringLiteral("history_*.png")}, QDir::Files, QDir::Name);
    while (files.size() > m_history.limit())
        files.removeFirst();
    for (const QString& f : files) {
        const QImage image(QDir(dir).filePath(f));
        if (!image.isNull())
            m_history.add({image, QRect(QPoint(0, 0), image.size()), QFileInfo(QDir(dir).filePath(f)).lastModified()});
    }
}

void App::quit()
{
    if (!m_dataDir.isEmpty() && m_settings.boolValue(QStringLiteral("pin.restoreOnStart")))
        m_pins->saveState(m_dataDir + QStringLiteral("/pins")); // PIN-54
    m_settings.saveToFile();
    QApplication::quit();
}

int App::restorePins()
{
    if (m_dataDir.isEmpty() || !m_settings.boolValue(QStringLiteral("pin.restoreOnStart")))
        return 0;
    const int n = m_pins->restoreState(m_dataDir + QStringLiteral("/pins"));
    rebuildGroupMenu();
    return n;
}

NotificationWidget* App::notify(const QString& title, const QString& message, const QString& filePath)
{
    if (!m_settings.boolValue(QStringLiteral("general.notifications")))
        return nullptr;
    auto* w = new NotificationWidget(title, message, filePath);
    w->show();
    return w;
}

} // namespace ss
