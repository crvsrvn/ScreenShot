#include "app/App.h"

#include "app/CaptureSession.h"
#include "app/PinWindow.h"
#include "core/util/Clock.h"

#include <QApplication>
#include <QClipboard>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QSystemTrayIcon>

namespace ss {

namespace {

QIcon makeTrayIcon()
{
    // 自行设计的简单图标：圆角方框 + 取景角标
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

App::App(Platform platform, QObject* parent)
    : QObject(parent)
    , m_platform(std::move(platform))
    , m_session(std::make_unique<CaptureSession>(m_platform.screenCapture.get()))
{
    connect(m_session.get(), &CaptureSession::captured, this, &App::onCaptured);
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
    m_trayMenu->addAction(tr("截图\tF1"), this, [this] { startCapture(monotonicNs()); });
    m_trayMenu->addAction(tr("贴图\tF3"), this, &App::pinFromClipboard);
    m_trayMenu->addSeparator();
    m_trayMenu->addAction(tr("退出"), this, &App::quit);
    m_tray = new QSystemTrayIcon(makeTrayIcon(), this);
    m_tray->setToolTip(QStringLiteral("ScreenShot"));
    m_tray->setContextMenu(m_trayMenu);
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger)
            startCapture(monotonicNs());
        else if (reason == QSystemTrayIcon::MiddleClick)
            pinFromClipboard();
    });
    m_tray->show();
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
    });
    m_hotkeyStatus[HotkeyCapture] = m_platform.hotkey->registerHotkey(HotkeyCapture, QKeySequence(Qt::Key_F1));
    m_hotkeyStatus[HotkeyPin] = m_platform.hotkey->registerHotkey(HotkeyPin, QKeySequence(Qt::Key_F3));
}

void App::useScreenCapture(std::unique_ptr<IScreenCapture> capture)
{
    m_injectedCapture = std::move(capture);
    m_session->setScreenCapture(m_injectedCapture ? m_injectedCapture.get() : m_platform.screenCapture.get());
}

void App::startCapture(qint64 triggerNs)
{
    m_session->start(triggerNs);
}

void App::onCaptured(const QImage& image, const QRect& physicalRegion)
{
    Q_UNUSED(physicalRegion);
    QApplication::clipboard()->setImage(image);
}

PinWindow* App::pinImage(const QImage& image, QPoint physicalPos)
{
    auto* pin = new PinWindow(image);
    pin->moveToPhysical(physicalPos);
    pin->fitToScreen();
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
    pinImage(image, QPoint(100, 100));
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

void App::quit()
{
    QApplication::quit();
}

} // namespace ss
