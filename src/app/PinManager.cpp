#include "app/PinManager.h"

#include "app/PinWindow.h"
#include "core/config/Settings.h"
#include "core/pin/ColorPin.h"
#include "core/pin/TextPin.h"

#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QImageReader>
#include <QMimeData>
#include <QScreen>
#include <QTimer>
#include <QUrl>

#if defined(Q_OS_WIN)
#include "platform/windows/WinShell.h"
#endif

namespace ss {

PinManager::PinManager(Settings& settings, OutputActions* output, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_output(output)
{
    m_groups.append(PinGroup{tr("Default"), {}, {}, {}, {}});
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

QPoint PinManager::resolvePosition(QPoint preferred, QSize physicalSize, bool fromCapture) const
{
    const QString policy = m_settings.stringValue(QStringLiteral("pin.newPosition"));
    auto physicalRect = [](QScreen* s) {
        const QRect g = s->geometry();
        return QRect(g.topLeft(), QSize(qRound(g.width() * s->devicePixelRatio()), qRound(g.height() * s->devicePixelRatio())));
    };
    auto centered = [&](QScreen* s) {
        if (!s)
            return preferred;
        const QRect r = physicalRect(s);
        return r.center() - QPoint(physicalSize.width() / 2, physicalSize.height() / 2);
    };
    if (policy == QLatin1String("primaryCenter"))
        return centered(QGuiApplication::primaryScreen());
    if (policy == QLatin1String("cursorScreenCenter")) {
        const QPoint cursor = QCursor::pos();
        QScreen* s = QGuiApplication::screenAt(cursor);
        return centered(s ? s : QGuiApplication::primaryScreen());
    }
    if (policy == QLatin1String("cursor") && fromCapture) {
        const QPoint cursor = QCursor::pos();
        QScreen* s = QGuiApplication::screenAt(cursor);
        if (!s)
            return preferred;
        return s->geometry().topLeft() + (cursor - s->geometry().topLeft()) * s->devicePixelRatio();
    }
    return preferred; // capture：截图位置；剪贴板贴图本来就用鼠标位置
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
    connect(pin, &PinWindow::ocrRequested, this, &PinManager::ocrRequested);
    connect(pin, &PinWindow::codesRequested, this, &PinManager::codesRequested);
    connect(pin, &PinWindow::ocrLinesRequested, this, &PinManager::ocrLinesRequested);
    connect(pin, &PinWindow::moveToGroupRequested, this, [this](PinWindow* p, int index) { movePinToGroup(p, index); });
    pin->setGroupColor(current().color);
    pin->setGroupNames(groupNames());
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

PinWindow* PinManager::handleDuplicate(const QImage& image)
{
    const QString policy = m_settings.stringValue(QStringLiteral("pin.duplicateBehavior"));
    if (policy == QLatin1String("duplicate") || image.isNull())
        return nullptr;
    const QImage probe = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    for (PinWindow* p : pins()) {
        if (p->originalImage() == probe) {
            if (policy == QLatin1String("shake")) {
                // 抖动已有贴图（PIN-14）
                const QPointF origin = p->contentTopLeft();
                for (int i = 0; i < 6; ++i)
                    QTimer::singleShot(30 * i, p, [p, origin, i] { p->moveContentTo(origin + QPointF(i == 5 ? 0 : (i % 2 ? 8 : -8), 0)); });
                p->raise();
                p->show();
            }
            return p;
        }
    }
    if (policy == QLatin1String("restore")) {
        PinGroup& g = current();
        for (int i = 0; i < g.closed.size(); ++i) {
            if (g.closed[i].image.convertToFormat(QImage::Format_ARGB32_Premultiplied) == probe) {
                const ClosedPin entry = g.closed.takeAt(i);
                auto* pin = new PinWindow(entry.image, m_settings, m_output);
                pin->applyStateJson(entry.state);
                return attach(pin, QPoint(entry.state.value(QStringLiteral("x")).toInt(), entry.state.value(QStringLiteral("y")).toInt()), entry.created);
            }
        }
    }
    return nullptr;
}

PinWindow* PinManager::pinImage(const QImage& image, QPoint physicalTopLeft)
{
    if (image.isNull())
        return nullptr;
    if (PinWindow* existing = handleDuplicate(image))
        return existing;
    auto* pin = new PinWindow(image, m_settings, m_output);
    return attach(pin, physicalTopLeft, QDateTime::currentDateTime());
}

PinWindow* PinManager::pinExplorerSelection(QPoint physicalTopLeft)
{
#if defined(Q_OS_WIN)
    QMimeData mime;
    QList<QUrl> urls;
    for (const QString& f : win::selectedExplorerFiles())
        urls.append(QUrl::fromLocalFile(f));
    if (urls.isEmpty())
        return nullptr;
    mime.setUrls(urls);
    return pinMime(&mime, physicalTopLeft);
#else
    Q_UNUSED(physicalTopLeft);
    return nullptr;
#endif
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

PinWindow* PinManager::pinFile(const QString& path, QPoint physicalTopLeft)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QImage image = reader.read(); // GIF 取首帧（PIN-03）
    if (!image.isNull())
        return pinImage(image, physicalTopLeft);
    if (!QFileInfo::exists(path) || m_settings.boolValue(QStringLiteral("pin.neverPinFilePaths")))
        return nullptr;
    auto* pin = new PinWindow(QImage(1, 1, QImage::Format_ARGB32_Premultiplied), m_settings, m_output);
    pin->setFileContent(path, scaleAt(physicalTopLeft));
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
        bool anyLocal = false;
        for (const QUrl& url : mime->urls()) {
            if (!url.isLocalFile())
                continue;
            anyLocal = true;
            if (PinWindow* pin = pinFile(url.toLocalFile(), physicalTopLeft + QPoint(offset, offset))) {
                last = pin;
                offset += 30;
            }
        }
        if (anyLocal)
            return last; // 本地文件列表不再退化成文本贴图
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
    if (m_solo)
        setSolo(false);
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

// ---- 独显 / 批量（PIN-61、PIN-62）----

void PinManager::setSolo(bool on)
{
    m_solo = on;
    for (PinWindow* p : pins())
        p->setVisible(!on || m_selected.contains(p));
    emit pinsChanged();
}

void PinManager::setAllThumbnails(bool on)
{
    for (PinWindow* p : pins())
        if (p->thumbnail() != on)
            p->toggleThumbnail();
}

int PinManager::saveAllToFolder(const QString& dir)
{
    QDir().mkpath(dir);
    int n = 0;
    for (PinWindow* p : pins()) {
        const QString path = QDir(dir).filePath(QStringLiteral("pin-%1.png").arg(n + 1, 3, 10, QLatin1Char('0')));
        if (p->displayedImage().save(path, "PNG"))
            ++n;
    }
    return n;
}

// ---- 分组（GRP-01、GRP-02、GRP-03）----

void PinManager::syncGroupInfo()
{
    const QStringList names = groupNames();
    for (int gi = 0; gi < m_groups.size(); ++gi)
        for (PinWindow* p : m_groups[gi].pins) {
            p->setGroupIndex(gi);
            p->setGroupColor(m_groups[gi].color);
            p->setGroupNames(names);
        }
}

bool PinManager::renameGroup(int index, const QString& name)
{
    if (index < 0 || index >= m_groups.size() || name.trimmed().isEmpty())
        return false;
    m_groups[index].name = name.trimmed();
    syncGroupInfo();
    emit groupChanged(m_current);
    return true;
}

bool PinManager::removeGroup(int index, int migrateTo)
{
    if (index < 0 || index >= m_groups.size() || migrateTo < 0 || migrateTo >= m_groups.size() || index == migrateTo || m_groups.size() <= 1)
        return false;
    PinGroup& from = m_groups[index];
    PinGroup& to = m_groups[migrateTo];
    for (PinWindow* p : from.pins) {
        to.pins.append(p);
        to.created.insert(p, from.created.value(p));
    }
    to.closed.append(from.closed);
    trimClosed(to);
    m_groups.removeAt(index);
    if (migrateTo > index)
        --migrateTo;
    const int current = m_current == index ? migrateTo : (m_current > index ? m_current - 1 : m_current);
    m_previous = std::clamp(m_previous > index ? m_previous - 1 : (m_previous == index ? current : m_previous), 0, static_cast<int>(m_groups.size()) - 1);
    m_current = current;
    for (int gi = 0; gi < m_groups.size(); ++gi)
        for (PinWindow* p : m_groups[gi].pins)
            p->setVisible(gi == m_current && !m_hidden);
    syncGroupInfo();
    emit groupChanged(m_current);
    emit pinsChanged();
    return true;
}

bool PinManager::moveGroup(int from, int to)
{
    if (from < 0 || to < 0 || from >= m_groups.size() || to >= m_groups.size() || from == to)
        return false;
    m_groups.move(from, to);
    auto remap = [from, to](int i) {
        if (i == from)
            return to;
        if (from < to && i > from && i <= to)
            return i - 1;
        if (from > to && i >= to && i < from)
            return i + 1;
        return i;
    };
    m_current = remap(m_current);
    m_previous = remap(m_previous);
    syncGroupInfo();
    emit groupChanged(m_current);
    return true;
}

void PinManager::setGroupColor(int index, const QColor& color)
{
    if (index < 0 || index >= m_groups.size())
        return;
    m_groups[index].color = color;
    syncGroupInfo();
}

QColor PinManager::groupColor(int index) const
{
    return index >= 0 && index < m_groups.size() ? m_groups[index].color : QColor();
}

bool PinManager::movePinToGroup(PinWindow* pin, int index)
{
    const int from = groupIndexOf(pin);
    if (from < 0 || index < 0 || index >= m_groups.size() || from == index)
        return false;
    m_groups[from].pins.removeOne(pin);
    m_groups[index].pins.append(pin);
    m_groups[index].created.insert(pin, m_groups[from].created.take(pin));
    m_selected.remove(pin);
    pin->setSelected(false);
    pin->setVisible(index == m_current && !m_hidden);
    syncGroupInfo();
    emit pinsChanged();
    return true;
}

std::pair<int, int> PinManager::groupCounts(int index) const
{
    if (index < 0 || index >= m_groups.size())
        return {0, 0};
    int onScreen = 0;
    for (PinWindow* p : m_groups[index].pins)
        if (p->isVisible())
            ++onScreen;
    return {onScreen, static_cast<int>(m_groups[index].pins.size())};
}

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
    m_groups.append(PinGroup{n, {}, {}, {}, {}});
    switchToGroup(static_cast<int>(m_groups.size()) - 1);
    syncGroupInfo();
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

// ---- 重启恢复（PIN-54）----

bool PinManager::saveState(const QString& dir) const
{
    QDir d(dir);
    d.mkpath(QStringLiteral("."));
    // 先清掉旧文件
    for (const QString& f : d.entryList({QStringLiteral("pin-*.png")}, QDir::Files))
        d.remove(f);
    QJsonArray groups;
    int fileIndex = 0;
    for (int gi = 0; gi < m_groups.size(); ++gi) {
        QJsonArray pins;
        for (PinWindow* pin : m_groups[gi].pins) {
            QJsonObject o = pin->stateJson();
            if (pin->contentKind() == PinWindow::ContentKind::Image) {
                // 显示图已含旋转 / 翻转 / 灰度 / 裁剪与标注：直接作为恢复底图，对应的变换状态不再重复应用
                const QString name = QStringLiteral("pin-%1.png").arg(fileIndex++);
                if (!pin->displayedImage().save(d.filePath(name), "PNG"))
                    continue;
                o[QStringLiteral("image")] = name;
                for (const char* k : {"rotation", "flipH", "flipV", "gray", "invert", "crop", "view"})
                    o.remove(QLatin1String(k));
            }
            o[QStringLiteral("visible")] = pin->isVisible() || m_hidden;
            pins.append(o);
        }
        groups.append(QJsonObject{{QStringLiteral("name"), m_groups[gi].name}, {QStringLiteral("pins"), pins},
                                  {QStringLiteral("color"), m_groups[gi].color.isValid() ? m_groups[gi].color.name(QColor::HexArgb) : QString()}});
    }
    QJsonObject rootObj{{QStringLiteral("groups"), groups}, {QStringLiteral("current"), m_current}};
    QFile f(d.filePath(QStringLiteral("pins.json")));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(QJsonDocument(rootObj).toJson(QJsonDocument::Compact));
    return true;
}

int PinManager::restoreState(const QString& dir)
{
    QFile f(QDir(dir).filePath(QStringLiteral("pins.json")));
    if (!f.open(QIODevice::ReadOnly))
        return 0;
    const QJsonObject rootObj = QJsonDocument::fromJson(f.readAll()).object();
    const QJsonArray groups = rootObj.value(QStringLiteral("groups")).toArray();
    int restored = 0;
    const int savedCurrent = rootObj.value(QStringLiteral("current")).toInt();
    for (int gi = 0; gi < groups.size(); ++gi) {
        const QJsonObject g = groups[gi].toObject();
        if (gi >= m_groups.size())
            m_groups.append(PinGroup{g.value(QStringLiteral("name")).toString(), {}, {}, {}, {}});
        else if (gi > 0)
            m_groups[gi].name = g.value(QStringLiteral("name")).toString();
        m_groups[gi].color = QColor::fromString(g.value(QStringLiteral("color")).toString());
        m_current = gi;
        for (const QJsonValue& v : g.value(QStringLiteral("pins")).toArray()) {
            const QJsonObject o = v.toObject();
            const QPoint at(o.value(QStringLiteral("x")).toInt(), o.value(QStringLiteral("y")).toInt());
            PinWindow* pin = nullptr;
            if (o.contains(QStringLiteral("image"))) {
                const QImage image(QDir(dir).filePath(o.value(QStringLiteral("image")).toString()));
                if (image.isNull())
                    continue;
                pin = new PinWindow(image, m_settings, m_output);
            } else if (o.contains(QStringLiteral("color"))) {
                pin = new PinWindow(QImage(1, 1, QImage::Format_ARGB32_Premultiplied), m_settings, m_output);
                pin->setColorContent(QColor::fromString(o.value(QStringLiteral("color")).toString()), scaleAt(at));
            } else if (o.contains(QStringLiteral("file"))) {
                pin = new PinWindow(QImage(1, 1, QImage::Format_ARGB32_Premultiplied), m_settings, m_output);
                pin->setFileContent(o.value(QStringLiteral("file")).toString(), scaleAt(at));
            } else if (o.contains(QStringLiteral("text"))) {
                TextPinOptions t;
                t.font = QFont(m_settings.stringValue(QStringLiteral("pin.textFont")), m_settings.intValue(QStringLiteral("pin.textSize")));
                t.color = QColor::fromString(m_settings.stringValue(QStringLiteral("pin.textColor")));
                t.background = QColor::fromString(m_settings.stringValue(QStringLiteral("pin.textBackground")));
                t.maxWidth = m_settings.intValue(QStringLiteral("pin.textMaxWidth"));
                t.padding = m_settings.intValue(QStringLiteral("pin.textPadding"));
                t.scale = scaleAt(at);
                t.html = o.value(QStringLiteral("html")).toBool();
                pin = new PinWindow(QImage(1, 1, QImage::Format_ARGB32_Premultiplied), m_settings, m_output);
                pin->setTextContent(o.value(QStringLiteral("text")).toString(), t);
            } else {
                continue;
            }
            pin->applyStateJson(o);
            attach(pin, at, QDateTime::currentDateTime());
            ++restored;
        }
        // 非当前组的贴图先藏起来
        if (gi != savedCurrent)
            for (PinWindow* pin : m_groups[gi].pins)
                pin->hide();
    }
    m_current = std::clamp(savedCurrent, 0, static_cast<int>(m_groups.size()) - 1);
    syncGroupInfo();
    emit groupChanged(m_current);
    emit pinsChanged();
    return restored;
}

} // namespace ss
