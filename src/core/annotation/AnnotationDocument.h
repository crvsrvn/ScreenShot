#pragma once

#include "core/annotation/Annotation.h"

#include <QImage>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QUndoStack>
#include <memory>
#include <vector>

namespace ss {

// 标注文档：只读底图 + 标注列表 + 命令栈（ANN-02、ANN-07）。
// 渲染只有一条路径：renderInto()，屏幕显示与导出都经过它（NFR-08）。
class AnnotationDocument : public QObject {
    Q_OBJECT
public:
    explicit AnnotationDocument(QObject* parent = nullptr);

    void setBaseImage(const QImage& image);
    const QImage& baseImage() const { return m_base; }
    QSize size() const { return m_base.size(); }

    // ---- 标注列表 ----
    int count() const { return static_cast<int>(m_items.size()); }
    const Annotation* at(int index) const { return m_items.at(index).get(); }
    Annotation* at(int index) { return m_items.at(index).get(); }
    int indexOf(int id) const;
    Annotation* find(int id);
    const Annotation* find(int id) const;
    // 最上层命中的标注（橡皮擦画笔不可选）
    Annotation* hitTest(QPointF p, double tolerance) const;

    // ---- 可撤销操作 ----
    int add(std::unique_ptr<Annotation> item);          // 返回 id
    void remove(int id);
    // 修改：调用方先 clone 出修改前的副本，改完后调用，记录为一条命令
    void commitModification(int id, std::unique_ptr<Annotation> before);
    void clearAll();                                    // 可撤销（ANN-02）
    QUndoStack& undoStack() { return m_undo; }
    bool canUndo() const { return m_undo.canUndo(); }
    bool canRedo() const { return m_undo.canRedo(); }
    void undo() { m_undo.undo(); }
    void redo() { m_undo.redo(); }

    // ---- 渲染 ----
    // 把底图与全部标注画进与底图同尺寸的图像；excludeId 不画（正在编辑的那个由调用方实时画）
    QImage render(int excludeId = -1) const;
    // 在已有画布上画（画布须与底图同尺寸、同格式），供临时标注实时预览
    void paintItem(QPainter& painter, const Annotation& item, const QImage& below) const;
    // 完成项缓存（NFR-05）：渲染结果按 (items 版本, excludeId) 缓存
    QImage cachedRender(int excludeId = -1) const;
    int revision() const { return m_revision; }

    // ---- 序列化 ----
    QJsonObject toJson() const;   // 只含标注，底图另存
    void fromJson(const QJsonObject& o);

signals:
    void changed();

private:
    friend class AddCommand;
    friend class RemoveCommand;
    friend class ModifyCommand;
    friend class ClearCommand;
    void insertRaw(int index, std::unique_ptr<Annotation> item);
    std::unique_ptr<Annotation> takeRaw(int id, int* index);
    void replaceRaw(int id, std::unique_ptr<Annotation> item);
    void bump();
    // 第 i 项之后的橡皮擦并集（用于裁剪第 i 项）
    QPainterPath erasersAfter(int index) const;

    QImage m_base;
    std::vector<std::unique_ptr<Annotation>> m_items;
    QUndoStack m_undo;
    int m_nextId = 1;
    int m_revision = 0;
    mutable QImage m_cache;
    mutable int m_cacheRevision = -1;
    mutable int m_cacheExclude = -2;
};

} // namespace ss
