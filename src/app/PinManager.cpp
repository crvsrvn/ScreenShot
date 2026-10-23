#include "app/PinManager.h"

#include "app/PinWindow.h"
#include "core/config/Settings.h"
#include "core/pin/TextPin.h"

#include <QApplication>
#include <QClipboard>
#include <QGuiApplication>
#include <QImageReader>
#include <QMimeData>
#include <QScreen>
#include <QUrl>

namespace ss {

PinManager::PinManager(Settings& settings, OutputActions* output, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_output(output)
{
    connect(QApplication::clipboard(), &QClipboard::dataChanged, this, [this] { m_clipboardDirty = true; });
}

PinManager::~PinManager()
{
    destroyAll();
}

PinWindow* PinManager::attach(PinWindow* pin, QPoint physicalTopLeft, const QDateTime& created)
{
    pin->moveToPhysical(physicalTopLeft);
    pin->fitToScreen();
    m_pins.append(pin);
    m_created.insert(pin, created);
    connect(pin, &PinWindow::closed, this, &PinManager::onClosed);
    connect(pin, &PinWindow::destroyed, this, &PinManager::onDestroyed);
    connect(pin, &PinWindow::pinRequested, this, [this](const QImage& image, QPoint at) { pinImage(image, at); });
    // 新建贴图时自动重新显示全部（PIN-52）
    if (m_hidden)
        showAll();
    pin->show();
    emit pinsChanged();
    return pin;
}

PinWindow* PinManager::pinImage(const QImage& image, QPoint physicalTopLeft)
{
    if (image.isNull())
        return nullptr;
    auto* pin = new PinWindow(image, m_settings, m_output);
    return attach(pin, physicalTopLeft, QDateTime::currentDateTime());
}

PinWindow* PinManager::pinText(const QString& text, QPoint physicalTopLeft)
{
    if (text.trimmed().isEmpty())
        return nullptr;
    TextPinOptions o;
    o.font = QFont(m_settings.stringValue(QStringLiteral("pin.textFont")), m_settings.intValue(QStringLiteral("pin.textSize")));
    o.color = QColor::fromString(m_settings.stringValue(QStringLiteral("pin.textColor")));
    o.background = QColor::fromString(m_settings.stringValue(QStringLiteral("pin.textBackground")));
    o.maxWidth = m_settings.intValue(QStringLiteral("pin.textMaxWidth"));
    o.padding = m_settings.intValue(QStringLiteral("pin.textPadding"));
    QScreen* screen = QGuiApplication::screenAt(physicalTopLeft);
    o.scale = screen ? screen->devicePixelRatio() : 1.0; // 按屏幕缩放比例自适应（PIN-04）
    return pinImage(renderTextPin(text, o), physicalTopLeft);
}

PinWindow* PinManager::pinClipboard(QPoint physicalTopLeft)
{
    const QMimeData* mime = QApplication::clipboard()->mimeData();
    m_clipboardDirty = false;
    if (!mime)
        return nullptr;
    if (mime->hasImage()) {
        const QImage image = qvariant_cast<QImage>(mime->imageData());
        if (!image.isNull())
            return pinImage(image, physicalTopLeft);
    }
    if (mime->hasUrls()) {
        PinWindow* last = nullptr;
        int offset = 0;
        for (const QUrl& url : mime->urls()) {
            if (!url.isLocalFile())
                continue;
            QImageReader reader(url.toLocalFile());
            reader.setAutoTransform(true);
            const QImage image = reader.read(); // GIF 取首帧（PIN-03）
            if (!image.isNull()) {
                last = pinImage(image, physicalTopLeft + QPoint(offset, offset));
                offset += 30;
            }
        }
        if (last)
            return last;
    }
    if (mime->hasText())
        return pinText(mime->text(), physicalTopLeft);
    return nullptr;
}

PinWindow* PinManager::pinHotkey(QPoint physicalTopLeft)
{
    if (m_clipboardDirty) {
        if (PinWindow* pin = pinClipboard(physicalTopLeft))
            return pin;
    }
    return restoreLastClosed();
}

PinWindow* PinManager::restoreLastClosed()
{
    if (m_closed.isEmpty())
        return nullptr;
    // 恢复顺序：最近关闭 / 最近创建（PIN-50）
    int index = 0;
    if (m_settings.stringValue(QStringLiteral("pin.restoreOrder")) == QLatin1String("created")) {
        for (int i = 1; i < m_closed.size(); ++i)
            if (m_closed[i].created > m_closed[index].created)
                index = i;
    }
    const ClosedPin entry = m_closed.takeAt(index);
    auto* pin = new PinWindow(entry.image, m_settings, m_output);
    pin->applyStateJson(entry.state);
    return attach(pin, QPoint(entry.state.value(QStringLiteral("x")).toInt(), entry.state.value(QStringLiteral("y")).toInt()), entry.created);
}

void PinManager::onClosed(PinWindow* pin)
{
    if (!m_pins.removeOne(pin))
        return;
    m_closed.prepend(ClosedPin{pin->originalImage(), pin->stateJson(), m_created.take(pin), QDateTime::currentDateTime()});
    // 关闭的贴图保留显示图（含标注）：用显示图作为恢复底图
    m_closed.first().image = pin->displayedImage();
    trimClosed();
    pin->deleteLater();
    emit pinsChanged();
}

void PinManager::onDestroyed(PinWindow* pin)
{
    if (!m_pins.removeOne(pin))
        return;
    m_created.remove(pin);
    pin->deleteLater();
    emit pinsChanged();
}

void PinManager::trimClosed()
{
    const int limit = m_settings.intValue(QStringLiteral("pin.restoreLimit"));
    while (m_closed.size() > limit)
        m_closed.removeLast();
}

void PinManager::hideAll()
{
    m_hidden = true;
    for (auto* p : std::as_const(m_pins))
        p->hide();
    emit pinsChanged();
}

void PinManager::showAll()
{
    m_hidden = false;
    for (auto* p : std::as_const(m_pins))
        p->show();
    emit pinsChanged();
}

void PinManager::toggleHidden()
{
    m_hidden ? showAll() : hideAll();
}

void PinManager::closeAll()
{
    const auto pins = m_pins;
    for (auto* p : pins)
        p->closePin();
}

void PinManager::destroyAll()
{
    const auto pins = m_pins;
    m_pins.clear();
    for (auto* p : pins) {
        p->hide();
        delete p;
    }
    m_created.clear();
    emit pinsChanged();
}

void PinManager::restoreOffscreen()
{
    QRect desktop;
    for (QScreen* s : QGuiApplication::screens())
        desktop = desktop.united(s->geometry());
    for (auto* p : std::as_const(m_pins)) {
        if (!desktop.intersects(p->contentRect().toRect()))
            p->moveContentTo(QGuiApplication::primaryScreen()->geometry().center() - QPointF(p->contentRect().width() / 2, p->contentRect().height() / 2));
    }
}

void PinManager::togglePassthroughAt(QPoint physical)
{
    // 从最上层（列表末尾最新）往下找光标下的贴图
    for (int i = m_pins.size() - 1; i >= 0; --i) {
        PinWindow* p = m_pins[i];
        if (p->isVisible() && p->physicalContentRect().contains(physical)) {
            p->setPassthrough(!p->passthrough());
            return;
        }
    }
    for (auto* p : std::as_const(m_pins))
        p->setPassthrough(false);
}

} // namespace ss
