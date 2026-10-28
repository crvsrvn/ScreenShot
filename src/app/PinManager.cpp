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
    connect(QApplication::clipboard(), &QClipboard::dataChanged, this, [this] { m_clipboardDirty = true; });
}

PinManager::~PinManager()
{
    destroyAll();
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
    m_pins.append(pin);
    m_created.insert(pin, created);
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
    // 吸附目标：其他可见贴图的内容矩形（PIN-21）
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
        for (int i = 0; i < m_closed.size(); ++i) {
            if (m_closed[i].image.convertToFormat(QImage::Format_ARGB32_Premultiplied) == probe) {
                const ClosedPin entry = m_closed.takeAt(i);
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
    m_selected.remove(pin);
    // 关闭的贴图保留显示图（含标注）：用显示图作为恢复底图
    m_closed.prepend(ClosedPin{pin->displayedImage(), pin->stateJson(), m_created.take(pin), QDateTime::currentDateTime()});
    trimClosed();
    pin->deleteLater();
    emit pinsChanged();
}

void PinManager::onDestroyed(PinWindow* pin)
{
    if (!m_pins.removeOne(pin))
        return;
    m_created.remove(pin);
    m_selected.remove(pin);
    pin->deleteLater();
    emit pinsChanged();
}

void PinManager::trimClosed()
{
    const int limit = m_settings.intValue(QStringLiteral("pin.restoreLimit"));
    while (m_closed.size() > limit)
        m_closed.removeLast();
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
    const auto list = m_pins;
    m_pins.clear();
    for (auto* p : list) {
        m_selected.remove(p);
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

// ---- 重启恢复（PIN-54）----

bool PinManager::saveState(const QString& dir) const
{
    QDir d(dir);
    d.mkpath(QStringLiteral("."));
    // 先清掉旧文件
    for (const QString& f : d.entryList({QStringLiteral("pin-*.png")}, QDir::Files))
        d.remove(f);
    QJsonArray pins;
    int fileIndex = 0;
    for (PinWindow* pin : m_pins) {
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
    const QJsonObject rootObj{{QStringLiteral("pins"), pins}};
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
    QJsonArray list = rootObj.value(QStringLiteral("pins")).toArray();
    for (const QJsonValue& g : rootObj.value(QStringLiteral("groups")).toArray()) // 旧版按贴图组保存
        for (const QJsonValue& v : g.toObject().value(QStringLiteral("pins")).toArray())
            list.append(v);
    int restored = 0;
    for (const QJsonValue& v : list) {
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
    emit pinsChanged();
    return restored;
}

} // namespace ss
