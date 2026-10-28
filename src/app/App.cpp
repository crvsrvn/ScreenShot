#include "app/App.h"

#include "app/WindowChrome.h"
#include "app/CaptureSession.h"
#include "app/CommandServer.h"
#include "core/ocr/OcrFormat.h"
#include "app/ResultDialogs.h"
#include "app/Theme.h"
#include "app/OutputActions.h"
#include "app/PinManager.h"
#include "app/PinWindow.h"
#include "app/PreferencesDialog.h"
#include "app/RecordPlaybackDialog.h"
#include "app/RecordSession.h"
#include "app/ScrollCaptureSession.h"
#include "core/config/Settings.h"
#include "core/output/FileNaming.h"
#include "core/util/Clock.h"

#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QDir>
#include <QRegularExpression>
#include <QTimer>
#include <QGuiApplication>
#include <QMenu>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QMimeData>
#include <QUrl>
#include <QSystemTrayIcon>

#if defined(Q_OS_WIN)
#include "platform/windows/WinInputHooks.h"
#include "platform/windows/WinSystem.h"
#include "platform/windows/WinWindowTools.h"
#elif defined(Q_OS_MACOS)
#include "platform/macos/MacPermissions.h"
#include "platform/macos/MacWindowTools.h"
#include <QPushButton>
#endif

namespace ss {

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
    connect(m_session.get(), &CaptureSession::scrollRequested, this, [this](const QRect& region) { startScroll(region); });
    connect(m_session.get(), &CaptureSession::recordRequested, this, [this](const QRect& region) { startRecord(region); });
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
            theme::message(nullptr, QMessageBox::Information, tr("Decode codes"), tr("No QR code or barcode found."));
        else
            showCodeResult(codes);
    });
    theme::apply(m_settings);
#ifdef Q_OS_WIN
    win::setProcessPriority(m_settings.stringValue(QStringLiteral("general.processPriority")));
#endif
    loadHistoryDir();
    if (m_platform.screenCapture)
        m_platform.screenCapture->configure(QStringLiteral("hdrStrategy"), m_settings.stringValue(QStringLiteral("capture.hdrStrategy")));
    connect(m_session.get(), &CaptureSession::captured, this, [this](const QImage&, const QRect& region) {
        m_settings.setValue(QStringLiteral("capture.lastRegion"), QStringLiteral("%1,%2,%3,%4").arg(region.x()).arg(region.y()).arg(region.width()).arg(region.height()));
    });
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
    m_trayMenu->addAction(tr("Settings"), this, &App::showPreferences);
    m_trayMenu->addAction(tr("Quit"), this, &App::quit);
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;
    m_tray = new QSystemTrayIcon(QApplication::windowIcon(), this);
    m_tray->setToolTip(QStringLiteral("ScreenShot"));
    m_tray->setContextMenu(m_trayMenu);
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) { trayActivated(reason); });
    m_tray->show();
}

void App::trayActivated(int reason)
{
    if (reason == QSystemTrayIcon::Trigger)
        startCapture(monotonicNs());
    else if (reason == QSystemTrayIcon::MiddleClick)
        pinFromClipboard();
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

void App::registerHotkeys()
{
    if (!m_platform.hotkey)
        return;
    for (int id : {HotkeyCapture, HotkeyPin, HotkeyTogglePins, HotkeyRestorePin, HotkeyPassthrough})
        m_platform.hotkey->unregisterHotkey(id);
    m_hotkeyStatus.clear();
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

void App::applySetting(const QString& key)
{
    if (key.startsWith(QStringLiteral("hotkey."))) {
        registerHotkeys();
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
    } else if (key.startsWith(QStringLiteral("ui."))) {
        theme::apply(m_settings);
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
    bool wait = command.contains(QStringLiteral("--wait"));
    QString result = QStringLiteral("ok");
    if (name == QLatin1String("snip")) {
        // snip [full|last|active-window|active-screen|cursor-screen|x,y,w,h]
        //      [--size WxH [--pos x,y]] [--delay ms] [--shadow] [--output copy,pin,save,ocrCopy] [--edit] [--wait]
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
        else {
            const QStringList parts = arg.split(QLatin1Char(','));
            if (parts.size() == 4)
                captureRegion(QRect(parts[0].toInt(), parts[1].toInt(), parts[2].toInt(), parts[3].toInt()), delay, edit);
        }
        if (wait && delay == 0 && !m_session->isActive()) {
            // 截图没有开始（已有截图在进行、区域为空、启动失败）：立即应答，不让调用方等到超时
            m_pendingReply = 0;
            result = QStringLiteral("error: capture did not start");
            wait = false;
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
    } else if (name == QLatin1String("record")) {
        // record x,y,w,h|last [--quick] [--start]：准备录制区域；--start 立即开始（按设置延迟）
        const QString arg = command.value(1);
        const QStringList p = (arg == QLatin1String("last") ? m_settings.stringValue(QStringLiteral("capture.lastRegion")) : arg).split(QLatin1Char(','));
        const QRect region = p.size() == 4 ? QRect(p[0].toInt(), p[1].toInt(), p[2].toInt(), p[3].toInt()) : QRect();
        if (region.isEmpty()) {
            result = QStringLiteral("error: region required (x,y,w,h or last)");
        } else if (!startRecord(region)) {
            result = QStringLiteral("error: ") + m_recordError;
        } else {
            if (command.contains(QStringLiteral("--quick")))
                m_record->setMode(RecordSession::Mode::Quick);
            if (command.contains(QStringLiteral("--start")))
                m_record->start();
        }
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
    } else if (name == QLatin1String("ocr-file")) {
        recognizeTextFromFile(command.value(1));
    } else if (name == QLatin1String("qr-file")) {
        decodeCodesFromFile(command.value(1));
    } else if (name == QLatin1String("toggle-pins")) {
        togglePinsHidden();
    } else if (name == QLatin1String("prefs")) {
        showPreferences();
    } else if (name == QLatin1String("quit")) {
        quit();
    } else if (!name.isEmpty()) {
        qWarning("unknown command: %s", qPrintable(name));
        result = QStringLiteral("unknown command");
    }
    finishCommand(requestId, result);
    return result;
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
        theme::message(nullptr, QMessageBox::Information, tr("Decode codes in image"), tr("No QR code or barcode found."));
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
    const QString text = theme::getText(nullptr, tr("Selection size"), tr("Width x Height (pixels)"),
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
        // CAP-73：完成后复制 / 保存 / 贴图（贴图上可继续标注）
        if (m_scrollResult.isNull())
            return;
        if (kind == QLatin1String("copy"))
            m_output->copy(m_scrollResult);
        else if (kind == QLatin1String("save"))
            m_output->saveWithDialog(m_scrollResult, nullptr);
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
    if (m_session->isActive()) {
        m_session->pin(); // 截图中：把当前选区贴到屏幕
        return;
    }
    m_pins->pinHotkey(cursorPhysical());
}

void App::restoreClosedPin()
{
    m_pins->restoreLastClosed();
}

void App::togglePinsHidden()
{
    m_pins->toggleHidden();
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

void App::applyInputHooks()
{
#ifdef Q_OS_WIN
    // 录制时显示点击与按键（VID-06）需要全局钩子，其余时间不装
    if (!m_recordHooks) {
        m_hooks.reset();
        return;
    }
    if (!m_hooks) {
        m_hooks = std::make_unique<win::WinInputHooks>();
        connect(m_hooks.get(), &win::WinInputHooks::keyDown, this, [this](int vk, bool, qint64) {
            if (m_record)
                m_record->noteKey(vk, true);
        });
        connect(m_hooks.get(), &win::WinInputHooks::keyUp, this, [this](int vk, bool, qint64) {
            if (m_record)
                m_record->noteKey(vk, false);
        });
        connect(m_hooks.get(), &win::WinInputHooks::mouseWheel, this, [this](int delta, QPoint pos) {
            if (m_record)
                m_record->noteWheel(delta, pos);
        });
        connect(m_hooks.get(), &win::WinInputHooks::mouseButton, this, [this](int button, bool down, QPoint pos) {
            if (m_record)
                m_record->noteMouseButton(button, down, pos);
        });
    }
    m_hooks->installKeyboard();
    m_hooks->installMouse();
#endif
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
    return m_pins->restoreState(m_dataDir + QStringLiteral("/pins"));
}

// ---- 屏幕录制（VID-01 ~ VID-09）----

RecordPlaybackDialog* App::playbackDialog() const
{
    return m_playback;
}

bool App::startRecord(const QRect& physicalRegion, bool showUi)
{
    if (m_record)
        m_record->cancel();
    m_record = std::make_unique<RecordSession>(m_session->screenCapture(), m_platform, m_settings, m_dataDir.isEmpty() ? QDir::tempPath() : m_dataDir);
    RecordSession* created = m_record.get();
    connect(created, &RecordSession::finished, this, &App::finishRecording);
    connect(created, &RecordSession::cancelled, this, [this, created] {
        m_recordHooks = false;
        applyInputHooks();
        // 排队释放只针对这个会话：startRecord 可能已经换成了新会话
        QMetaObject::invokeMethod(this, [this, created] { if (m_record.get() == created) m_record.reset(); }, Qt::QueuedConnection);
    });
    if (!m_record->begin(physicalRegion, showUi)) {
        m_recordError = m_record->lastError();
        m_record.reset();
        return false;
    }
    // 输入可视化需要全局钩子（VID-06）
    m_recordHooks = m_settings.boolValue(QStringLiteral("record.showClicks")) || m_settings.boolValue(QStringLiteral("record.showKeys"));
    applyInputHooks();
    return true;
}

void App::finishRecording()
{
    m_recordHooks = false;
    applyInputHooks();
    if (m_record->mode() == RecordSession::Mode::Quick) {
        // 快速录制（VID-03）：文件已写好，移到保存目录并复制为文件
        const QString src = m_record->quickFilePath();
        QDir().mkpath(m_output->defaultSaveDir());
        const QString dst = uniqueFilePath(m_output->defaultSaveDir(), m_output->makeFileName(QImage()), QStringLiteral("mp4"));
        const bool moved = QFile::rename(src, dst) || (QFile::copy(src, dst) && QFile::remove(src));
        if (moved) {
            auto* mime = new QMimeData;
            mime->setUrls({QUrl::fromLocalFile(dst)});
            QGuiApplication::clipboard()->setMimeData(mime);
        }
        m_recordLastFile = moved ? dst : src;
    } else if (m_record->store()) {
        m_playback = new RecordPlaybackDialog(m_record->store(), m_settings, *m_output, m_platform.videoEncoder);
        m_playback->setAttribute(Qt::WA_DeleteOnClose);
        connect(m_playback, &RecordPlaybackDialog::exported, this, [this](const QString& path) { m_recordLastFile = path; });
        m_playback->show();
    }
    RecordSession* done = m_record.get();
    QMetaObject::invokeMethod(this, [this, done] { if (m_record.get() == done) m_record.reset(); }, Qt::QueuedConnection);
}

QStringList App::unfinishedRecordings() const
{
    QStringList out;
    const QDir dir(QDir(m_dataDir.isEmpty() ? QDir::tempPath() : m_dataDir).filePath(QStringLiteral("recordings")));
    for (const QFileInfo& fi : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (FrameStore::hasUnfinished(fi.absoluteFilePath()))
            out.append(fi.absoluteFilePath());
    }
    return out;
}

RecordPlaybackDialog* App::openRecording(const QString& dir)
{
    auto store = std::make_shared<FrameStore>();
    if (!store->open(dir) || store->frameCount() == 0)
        return nullptr;
    store->writeIndex(true);
    m_playback = new RecordPlaybackDialog(store, m_settings, *m_output, m_platform.videoEncoder);
    m_playback->setAttribute(Qt::WA_DeleteOnClose);
    connect(m_playback, &RecordPlaybackDialog::exported, this, [this](const QString& path) { m_recordLastFile = path; });
    m_playback->show();
    return m_playback;
}

int App::recoverRecordings()
{
    int opened = 0;
    for (const QString& dir : unfinishedRecordings()) {
        const auto answer = theme::message(nullptr, QMessageBox::Question, tr("Recover recording"), tr("A recording was interrupted. Open it now?"), QMessageBox::Yes | QMessageBox::Discard);
        if (answer == QMessageBox::Yes && openRecording(dir))
            ++opened;
        else
            QDir(dir).removeRecursively();
    }
    return opened;
}

} // namespace ss
