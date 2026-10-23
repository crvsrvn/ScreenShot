#include "core/annotation/AnnotationDocument.h"

#include <QJsonArray>
#include <QPainter>

namespace ss {

// ---- 命令 ----

class AddCommand : public QUndoCommand {
public:
    AddCommand(AnnotationDocument* doc, std::unique_ptr<Annotation> item)
        : m_doc(doc)
        , m_item(std::move(item))
        , m_id(m_item->id())
    {
        setText(QStringLiteral("add"));
    }
    void redo() override { m_doc->insertRaw(-1, std::move(m_item)); }
    void undo() override { m_item = m_doc->takeRaw(m_id, &m_index); }

private:
    AnnotationDocument* m_doc;
    std::unique_ptr<Annotation> m_item;
    int m_id;
    int m_index = -1;
};

class RemoveCommand : public QUndoCommand {
public:
    RemoveCommand(AnnotationDocument* doc, int id)
        : m_doc(doc)
        , m_id(id)
    {
        setText(QStringLiteral("remove"));
    }
    void redo() override { m_item = m_doc->takeRaw(m_id, &m_index); }
    void undo() override { m_doc->insertRaw(m_index, std::move(m_item)); }

private:
    AnnotationDocument* m_doc;
    int m_id;
    int m_index = -1;
    std::unique_ptr<Annotation> m_item;
};

class ModifyCommand : public QUndoCommand {
public:
    ModifyCommand(AnnotationDocument* doc, int id, std::unique_ptr<Annotation> before, std::unique_ptr<Annotation> after)
        : m_doc(doc)
        , m_id(id)
        , m_before(std::move(before))
        , m_after(std::move(after))
    {
        setText(QStringLiteral("modify"));
    }
    void redo() override { m_doc->replaceRaw(m_id, m_after->clone()); }
    void undo() override { m_doc->replaceRaw(m_id, m_before->clone()); }

private:
    AnnotationDocument* m_doc;
    int m_id;
    std::unique_ptr<Annotation> m_before;
    std::unique_ptr<Annotation> m_after;
};

class ClearCommand : public QUndoCommand {
public:
    explicit ClearCommand(AnnotationDocument* doc)
        : m_doc(doc)
    {
        setText(QStringLiteral("clear"));
    }
    void redo() override
    {
        m_items.clear();
        while (m_doc->count() > 0) {
            int index = 0;
            m_items.push_back(m_doc->takeRaw(m_doc->at(0)->id(), &index));
        }
    }
    void undo() override
    {
        for (auto& item : m_items)
            m_doc->insertRaw(-1, std::move(item));
        m_items.clear();
    }

private:
    AnnotationDocument* m_doc;
    std::vector<std::unique_ptr<Annotation>> m_items;
};

// ---- 文档 ----

AnnotationDocument::AnnotationDocument(QObject* parent)
    : QObject(parent)
{
}

void AnnotationDocument::setBaseImage(const QImage& image)
{
    m_base = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    bump();
}

int AnnotationDocument::indexOf(int id) const
{
    for (int i = 0; i < static_cast<int>(m_items.size()); ++i)
        if (m_items[i]->id() == id)
            return i;
    return -1;
}

Annotation* AnnotationDocument::find(int id)
{
    const int i = indexOf(id);
    return i < 0 ? nullptr : m_items[i].get();
}

const Annotation* AnnotationDocument::find(int id) const
{
    const int i = indexOf(id);
    return i < 0 ? nullptr : m_items[i].get();
}

Annotation* AnnotationDocument::hitTest(QPointF p, double tolerance) const
{
    for (int i = static_cast<int>(m_items.size()) - 1; i >= 0; --i) {
        if (m_items[i]->hitTest(p, tolerance))
            return m_items[i].get();
    }
    return nullptr;
}

void AnnotationDocument::insertRaw(int index, std::unique_ptr<Annotation> item)
{
    if (index < 0 || index > static_cast<int>(m_items.size()))
        m_items.push_back(std::move(item));
    else
        m_items.insert(m_items.begin() + index, std::move(item));
    bump();
}

std::unique_ptr<Annotation> AnnotationDocument::takeRaw(int id, int* index)
{
    const int i = indexOf(id);
    if (index)
        *index = i;
    if (i < 0)
        return nullptr;
    std::unique_ptr<Annotation> item = std::move(m_items[i]);
    m_items.erase(m_items.begin() + i);
    bump();
    return item;
}

void AnnotationDocument::replaceRaw(int id, std::unique_ptr<Annotation> item)
{
    const int i = indexOf(id);
    if (i < 0)
        return;
    item->setId(id);
    m_items[i] = std::move(item);
    bump();
}

void AnnotationDocument::bump()
{
    ++m_revision;
    emit changed();
}

int AnnotationDocument::add(std::unique_ptr<Annotation> item)
{
    if (!item)
        return -1;
    const int id = m_nextId++;
    item->setId(id);
    m_undo.push(new AddCommand(this, std::move(item)));
    return id;
}

void AnnotationDocument::remove(int id)
{
    if (indexOf(id) >= 0)
        m_undo.push(new RemoveCommand(this, id));
}

void AnnotationDocument::commitModification(int id, std::unique_ptr<Annotation> before)
{
    const Annotation* current = find(id);
    if (!current || !before)
        return;
    m_undo.push(new ModifyCommand(this, id, std::move(before), current->clone()));
}

void AnnotationDocument::clearAll()
{
    if (!m_items.empty())
        m_undo.push(new ClearCommand(this));
}

QPainterPath AnnotationDocument::erasersAfter(int index) const
{
    QPainterPath path;
    for (int j = index + 1; j < static_cast<int>(m_items.size()); ++j) {
        const QPainterPath e = m_items[j]->eraseShape();
        if (!e.isEmpty())
            path = path.isEmpty() ? e : path.united(e);
    }
    return path;
}

void AnnotationDocument::paintItem(QPainter& painter, const Annotation& item, const QImage& below) const
{
    RenderContext ctx;
    ctx.below = &below;
    ctx.base = &m_base;
    item.paint(painter, ctx);
}

QImage AnnotationDocument::render(int excludeId) const
{
    QImage out = m_base.copy();
    if (out.isNull())
        return out;
    out.setDevicePixelRatio(1.0);
    for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
        const Annotation& item = *m_items[i];
        if (item.id() == excludeId || item.isEraser())
            continue;
        QPainter p(&out);
        // 被后面的橡皮擦擦掉的部分不画（ANN-36：只擦标注不擦底图）
        const QPainterPath erased = erasersAfter(i);
        if (!erased.isEmpty()) {
            QPainterPath keep;
            keep.addRect(out.rect());
            p.setClipPath(keep.subtracted(erased));
        }
        RenderContext ctx;
        ctx.below = &out; // 同一画布：效果类标注读取当前已画内容（含更早的标注）
        ctx.base = &m_base;
        item.paint(p, ctx);
    }
    return out;
}

QImage AnnotationDocument::cachedRender(int excludeId) const
{
    if (m_cacheRevision != m_revision || m_cacheExclude != excludeId || m_cache.isNull()) {
        m_cache = render(excludeId);
        m_cacheRevision = m_revision;
        m_cacheExclude = excludeId;
    }
    return m_cache;
}

QJsonObject AnnotationDocument::toJson() const
{
    QJsonArray items;
    for (const auto& item : m_items)
        items.append(item->toJson());
    return {{QStringLiteral("items"), items}, {QStringLiteral("nextId"), m_nextId}};
}

void AnnotationDocument::fromJson(const QJsonObject& o)
{
    m_items.clear();
    m_undo.clear();
    for (const auto& v : o.value(QStringLiteral("items")).toArray()) {
        if (auto item = annotationFromJson(v.toObject()))
            m_items.push_back(std::move(item));
    }
    m_nextId = o.value(QStringLiteral("nextId")).toInt(1);
    for (const auto& item : m_items)
        m_nextId = std::max(m_nextId, item->id() + 1);
    bump();
}

} // namespace ss
