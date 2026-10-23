#include "app/PinManager.h"

#include "app/PinWindow.h"
#include "core/config/Settings.h"
#include "core/pin/ColorPin.h"
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
    m_groups.append(PinGroup{tr("Default"), {}, {}, {}});
    connect(QApplication::clipboard(), &QClipboard::dataChanged, this, [this] { m_clipboardDirty = true; });
}

PinManager::~PinManager()
{
    for (int i = 0; i < m_groups.size(); ++i) {
        m_current = i;
        destroyAll();
    }
}

qreal PinManager::scaleAt(QPoint physical) const
{
    QScreen* screen = QGuiApplication::screenAt(physical);
    return screen ? screen->devicePixelRatio() : 1.0; // 按屏幕缩放比例自适应（PIN-04）
}

PinWindow* PinManager::attach(PinWindow* pin, QPoint physicalTopLeft, const QDateTime& created)
{
    pin->moveToPhysical(physicalTopLeft);
    pin->fitToScreen();
    pin->setGroupIndex(m_current);
    PinGroup& g = current();
    g.pins.append(pin);
    g.created.insert(pin, created);
    connect(pin, &PinWindow::closed, this, &PinManager::onClosed);
    connect(pin, &PinWindow::destroyed, this, &PinManager::onDestroyed);
    connect(pin, &PinWindow::pinRequested, this, [this](const QImage& image, QPoint at) { pinImage(image, at); });
    connect(pin, &PinWindow::replaceRequested, this, [this](PinWindow* p, const QMimeData* mime) { replacePin(p, mime); });
    connect(pin, &PinWindow::selectToggleRequested, this, &PinManager::toggleSelected);
    connect(pin, &PinWindow::selectAllRequested, this, &PinManager::selectAllVisible);
    connect(pin, &PinWindow::groupMoved, this, [this](PinWindow* origin, QPointF delta) {
        for (PinWindow* other : selectedPins())
            if (other != origin)
                other->moveContentTo(other->contentTopLeft() + delta);
    });
    connect(pin, &PinWindow::groupAction, this, &PinManager::onGroupAction);
    // 吸附目标：同组其他可见贴图的内容矩形（PIN-21）
    pin->setSnapTargets([this, pin] {
        QList<QRectF> rects;
        for (PinWindow* other : pins())
            if (other != pin && other->isVisible())
                rects.append(other->contentRect());
        return rects;
    });
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
    o.scale = scaleAt(physicalTopLeft);
    auto* pin = new PinWindow(QImage(1, 1, QImage::Format_ARGB32_Premultiplied), m_settings, m_output);
    pin->setTextContent(text, o);
    return attach(pin, physicalTopLeft, QDateTime::currentDateTime());
}

PinWindow* PinManager::pinHtml(const QString& html, QPoint physicalTopLeft)
{
    if (html.trimmed().isEmpty())
        return nullptr;
    TextPinOptions o;
    o.font = QFont(m_settings.stringValue(QStringLiteral("pin.textFont")), m_settings.intValue(QStringLiteral("pin.textSize")));
    o.color = QColor::fromString(m_settings.stringValue(QStringLiteral("pin.textColor")));
    o.background = QColor::fromString(m_settings.stringValue(QStringLiteral("pin.textBackground")));
    o.maxWidth = m_settings.intValue(QStringLiteral("pin.textMaxWidth"));
    o.padding = m_settings.intValue(QStringLiteral("pin.textPadding"));
    o.scale = scaleAt(physicalTopLeft);
    o.html = true;
    auto* pin = new PinWindow(QImage(1, 1, QImage::Format_ARGB32_Premultiplied), m_settings, m_output);
    pin->setTextContent(html, o);
    return attach(pin, physicalTopLeft, QDateTime::currentDateTime());
}

PinWindow* PinManager::pinColor(const QColor& color, QPoint physicalTopLeft)
{
    auto* pin = new PinWindow(QImage(1, 1, QImage::Format_ARGB32_Premultiplied), m_settings, m_output);
    pin->setColorContent(color, scaleAt(physicalTopLeft));
    return attach(pin, physicalTopLeft, QDateTime::currentDateTime());
}

PinWindow* PinManager::pinMime(const QMimeData* mime, QPoint physicalTopLeft)
{
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
    if (mime->hasText()) {
        if (const auto color = parseColorText(mime->text()))
            return pinColor(*color, physicalTopLeft); // PIN-07
        if (mime->hasHtml() && m_settings.boolValue(QStringLiteral("pin.textHtml")))
            return pinHtml(mime->html(), physicalTopLeft); // PIN-05
        return pinText(mime->text(), physicalTopLeft);
    }
    return nullptr;
}

PinWindow* PinManager::pinClipboard(QPoint physicalTopLeft)
{
    m_clipboardDirty = false;
    return pinMime(QApplication::clipboard()->mimeData(), physicalTopLeft);
}

PinWindow* PinManager::pinHotkey(QPoint physicalTopLeft)
{
    if (m_clipboardDirty) {
        if (PinWindow* pin = pinClipboard(physicalTopLeft))
            return pin;
    }
    return restoreLastClosed();
}

PinWindow* PinManager::replacePin(PinWindow* pin, const QMimeData* mime)
{
    if (!pin || !mime)
        return nullptr;
    const QPoint at = pin->physicalTopLeft();
    const QString policy = m_settings.stringValue(QStringLiteral("pin.replacePolicy"));
    PinWindow* fresh = pinMime(mime, policy == QLatin1String("keep") ? at + QPoint(30, 30) : at);
    if (!fresh)
        return nullptr;
    if (policy == QLatin1String("close"))
        pin->closePin();
    else if (policy == QLatin1String("destroy"))
        pin->destroyPin();
    return fresh;
}

PinWindow* PinManager::restoreLastClosed()
{
    PinGroup& g = current();
    if (g.closed.isEmpty())
        return nullptr;
    // 恢复顺序：最近关闭 / 最近创建（PIN-50）
    int index = 0;
    if (m_settings.stringValue(QStringLiteral("pin.restoreOrder")) == QLatin1String("created")) {
        for (int i = 1; i < g.closed.size(); ++i)
            if (g.closed[i].created > g.closed[index].created)
                index = i;
    }
    const ClosedPin entry = g.closed.takeAt(index);
    auto* pin = new PinWindow(entry.image, m_settings, m_output);
    pin->applyStateJson(entry.state);
    return attach(pin, QPoint(entry.state.value(QStringLiteral("x")).toInt(), entry.state.value(QStringLiteral("y")).toInt()), entry.created);
}

void PinManager::onClosed(PinWindow* pin)
{
    const int gi = groupIndexOf(pin);
    if (gi < 0)
        return;
    PinGroup& g = m_groups[gi];
    g.pins.removeOne(pin);
    m_selected.remove(pin);
    g.closed.prepend(ClosedPin{pin->originalImage(), pin->stateJson(), g.created.take(pin), QDateTime::currentDateTime()});
    // 关闭的贴图保留显示图（含标注）：用显示图作为恢复底图
    g.closed.first().image = pin->displayedImage();
    trimClosed(g);
    pin->deleteLater();
    emit pinsChanged();
}

void PinManager::onDestroyed(PinWindow* pin)
{
    const int gi = groupIndexOf(pin);
    if (gi < 0)
        return;
    m_groups[gi].pins.removeOne(pin);
    m_groups[gi].created.remove(pin);
    m_selected.remove(pin);
    pin->deleteLater();
    emit pinsChanged();
}

void PinManager::trimClosed(PinGroup& g)
{
    const int limit = m_settings.intValue(QStringLiteral("pin.restoreLimit"));
    while (g.closed.size() > limit)
        g.closed.removeLast();
}

bool PinManager::confirmBatch(int count, const QString& verb)
{
    if (count <= 1 || !m_settings.boolValue(QStringLiteral("pin.confirmBatch")))
        return true;
    return PinWindow::confirmHandler(nullptr, tr("%1 %2 pins?").arg(verb).arg(count));
}

void PinManager::hideAll()
{
    m_hidden = true;
    for (auto* p : pins())
        p->hide();
    emit pinsChanged();
}

void PinManager::showAll()
{
    m_hidden = false;
    for (auto* p : pins())
        p->show();
    emit pinsChanged();
}

void PinManager::toggleHidden()
{
    m_hidden ? showAll() : hideAll();
}

void PinManager::closeAll()
{
    const auto list = pins();
    if (!confirmBatch(static_cast<int>(list.size()), tr("Close")))
        return;
    for (auto* p : list) {
        p->setSelected(false);
        p->closePin();
    }
}

void PinManager::destroyAll()
{
    PinGroup& g = current();
    const auto list = g.pins;
    g.pins.clear();
    for (auto* p : list) {
        m_selected.remove(p);
        p->hide();
        delete p;
    }
    g.created.clear();
    emit pinsChanged();
}

void PinManager::restoreOffscreen()
{
    QRect desktop;
    for (QScreen* s : QGuiApplication::screens())
        desktop = desktop.united(s->geometry());
    for (auto* p : pins()) {
        if (!desktop.intersects(p->contentRect().toRect()))
            p->moveContentTo(QGuiApplication::primaryScreen()->geometry().center() - QPointF(p->contentRect().width() / 2, p->contentRect().height() / 2));
    }
}

void PinManager::togglePassthroughAt(QPoint physical)
{
    // 从最上层（列表末尾最新）往下找光标下的贴图
    const auto list = pins();
    for (int i = list.size() - 1; i >= 0; --i) {
        PinWindow* p = list[i];
        if (p->isVisible() && p->physicalContentRect().contains(physical)) {
            p->setPassthrough(!p->passthrough());
            return;
        }
    }
    for (auto* p : list)
        p->setPassthrough(false);
}

// ---- 多选（PIN-60）----

QList<PinWindow*> PinManager::selectedPins() const
{
    QList<PinWindow*> out;
    for (PinWindow* p : pins())
        if (m_selected.contains(p))
            out.append(p);
    return out;
}

void PinManager::toggleSelected(PinWindow* pin)
{
    if (m_selected.contains(pin)) {
        m_selected.remove(pin);
        pin->setSelected(false);
    } else {
        m_selected.insert(pin);
        pin->setSelected(true);
    }
}

void PinManager::selectAllVisible()
{
    for (PinWindow* p : pins()) {
        if (p->isVisible()) {
            m_selected.insert(p);
            p->setSelected(true);
        }
    }
}

void PinManager::clearSelection()
{
    for (PinWindow* p : pins())
        p->setSelected(false);
    m_selected.clear();
}

void PinManager::onGroupAction(PinWindow* origin, const QString& action, double arg)
{
    Q_UNUSED(origin);
    const QList<PinWindow*> targets = selectedPins();
    if (action == QLatin1String("close") || action == QLatin1String("destroy")) {
        if (!confirmBatch(static_cast<int>(targets.size()), action == QLatin1String("close") ? tr("Close") : tr("Destroy")))
            return;
        for (PinWindow* p : targets) {
            p->setSelected(false);
            m_selected.remove(p);
            action == QLatin1String("close") ? p->closePin() : p->destroyPin();
        }
        return;
    }
    for (PinWindow* p : targets) {
        if (action == QLatin1String("reset"))
            p->toggleReset();
        else if (action == QLatin1String("opacity"))
            p->adjustOpacity(static_cast<int>(arg));
    }
}

// ---- 分组（GRP-01）----

QStringList PinManager::groupNames() const
{
    QStringList names;
    for (const auto& g : m_groups)
        names.append(g.name);
    return names;
}

int PinManager::groupIndexOf(PinWindow* pin) const
{
    for (int i = 0; i < m_groups.size(); ++i)
        if (m_groups[i].pins.contains(pin))
            return i;
    return -1;
}

int PinManager::newGroup(const QString& name)
{
    const QString n = name.trimmed().isEmpty() ? tr("Group %1").arg(m_groups.size() + 1) : name.trimmed();
    m_groups.append(PinGroup{n, {}, {}, {}});
    switchToGroup(static_cast<int>(m_groups.size()) - 1);
    return m_current;
}

bool PinManager::switchToGroup(int index)
{
    if (index < 0 || index >= m_groups.size() || index == m_current)
        return false;
    clearSelection();
    for (auto* p : current().pins)
        p->hide();
    m_previous = m_current;
    m_current = index;
    m_hidden = false;
    for (auto* p : current().pins)
        p->show();
    emit groupChanged(m_current);
    emit pinsChanged();
    return true;
}

bool PinManager::switchToGroupNamed(const QString& name)
{
    for (int i = 0; i < m_groups.size(); ++i)
        if (m_groups[i].name.compare(name, Qt::CaseInsensitive) == 0)
            return i == m_current || switchToGroup(i);
    return false;
}

void PinManager::nextGroup()
{
    if (m_groups.size() > 1)
        switchToGroup((m_current + 1) % static_cast<int>(m_groups.size()));
}

void PinManager::previousGroup()
{
    switchToGroup(m_previous);
}

void PinManager::clearCurrentGroup()
{
    destroyAll();
    current().closed.clear();
    emit pinsChanged();
}

} // namespace ss
