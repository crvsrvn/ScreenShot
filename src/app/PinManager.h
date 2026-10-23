#pragma once

#include <QDateTime>
#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPoint>
#include <QSet>

class QMimeData;

namespace ss {

class PinWindow;
class Settings;
class OutputActions;

// 已关闭的贴图（PIN-50）：可恢复
struct ClosedPin {
    QImage image;
    QJsonObject state;
    QDateTime created;
    QDateTime closed;
};

// 贴图分组（GRP-01）：每组有独立的屏幕贴图与关闭历史
struct PinGroup {
    QString name;
    QList<PinWindow*> pins;
    QList<ClosedPin> closed;
    QHash<PinWindow*, QDateTime> created;
};

// 贴图管理（PIN-01、PIN-03、PIN-04、PIN-11、PIN-33、PIN-50 ~ PIN-53、PIN-55、PIN-60、GRP-01）
class PinManager : public QObject {
    Q_OBJECT
public:
    PinManager(Settings& settings, OutputActions* output, QObject* parent = nullptr);
    ~PinManager() override;

    // 当前组
    QList<PinWindow*> pins() const { return current().pins; }
    const QList<ClosedPin>& closedPins() const { return current().closed; }
    bool allHidden() const { return m_hidden; }

    PinWindow* pinImage(const QImage& image, QPoint physicalTopLeft);
    PinWindow* pinText(const QString& text, QPoint physicalTopLeft);
    PinWindow* pinHtml(const QString& html, QPoint physicalTopLeft);       // PIN-05
    PinWindow* pinColor(const QColor& color, QPoint physicalTopLeft);      // PIN-07
    PinWindow* pinFile(const QString& path, QPoint physicalTopLeft);       // PIN-08：图片贴图片，否则文件卡片
    // 新贴图位置策略（PIN-13）：preferred 为截图位置 / 鼠标位置；fromCapture 表示来自截图
    QPoint resolvePosition(QPoint preferred, QSize physicalSize, bool fromCapture) const;
    // 重启恢复（PIN-54）：把未关闭的贴图（含分组）存到 dir，启动时读回
    bool saveState(const QString& dir) const;
    int restoreState(const QString& dir);
    // 任意 MIME 内容：图像 → 图像贴图；图片文件 → 每个文件一个贴图；颜色文本 → 色卡；HTML → 富文本；文本 → 文本贴图
    PinWindow* pinMime(const QMimeData* mime, QPoint physicalTopLeft);
    PinWindow* pinClipboard(QPoint physicalTopLeft);
    // 贴图热键：剪贴板自上次贴图后有变化就贴剪贴板，否则恢复最近关闭的贴图
    PinWindow* pinHotkey(QPoint physicalTopLeft);
    PinWindow* restoreLastClosed();
    // 替换（PIN-11）：按设置处理原贴图（保留 / 关闭 / 销毁），新内容贴在原位置
    PinWindow* replacePin(PinWindow* pin, const QMimeData* mime);

    void hideAll();
    void showAll();
    void toggleHidden();
    void closeAll();   // 多个时按设置确认（PIN-53）
    void destroyAll();
    void restoreOffscreen(); // PIN-55
    // 切换鼠标下方贴图的穿透；下方没有贴图时取消全部穿透（PIN-33）
    void togglePassthroughAt(QPoint physical);

    // ---- 多选（PIN-60）----
    QList<PinWindow*> selectedPins() const;
    void toggleSelected(PinWindow* pin);
    void selectAllVisible();
    void clearSelection();

    // ---- 分组（GRP-01）----
    QStringList groupNames() const;
    int currentGroupIndex() const { return m_current; }
    QString currentGroupName() const { return current().name; }
    int newGroup(const QString& name);          // 新建并切换；返回索引
    bool switchToGroup(int index);
    bool switchToGroupNamed(const QString& name);
    void nextGroup();
    void previousGroup();                       // 回到上一个组
    void clearCurrentGroup();                   // 销毁当前组全部贴图并清空历史
    int groupIndexOf(PinWindow* pin) const;

    // 测试：模拟剪贴板变化
    void markClipboardDirty() { m_clipboardDirty = true; }
    bool clipboardDirty() const { return m_clipboardDirty; }

signals:
    void pinsChanged();
    void groupChanged(int index);
    void ocrRequested(const QImage& image);
    void codesRequested(const QImage& image);

private:
    PinGroup& current() { return m_groups[m_current]; }
    const PinGroup& current() const { return m_groups[m_current]; }
    PinWindow* attach(PinWindow* pin, QPoint physicalTopLeft, const QDateTime& created);
    void onClosed(PinWindow* pin);
    void onDestroyed(PinWindow* pin);
    void onGroupAction(PinWindow* origin, const QString& action, double arg);
    void trimClosed(PinGroup& group);
    bool confirmBatch(int count, const QString& verb);
    qreal scaleAt(QPoint physical) const;

    Settings& m_settings;
    OutputActions* m_output;
    QList<PinGroup> m_groups;
    int m_current = 0;
    int m_previous = 0;
    QSet<PinWindow*> m_selected;
    bool m_hidden = false;
    bool m_clipboardDirty = true;
};

} // namespace ss
