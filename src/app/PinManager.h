#pragma once

#include <QDateTime>
#include <QImage>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPoint>

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

// 贴图管理（PIN-01、PIN-03、PIN-04、PIN-33、PIN-50 ~ PIN-52、PIN-55）
class PinManager : public QObject {
    Q_OBJECT
public:
    PinManager(Settings& settings, OutputActions* output, QObject* parent = nullptr);
    ~PinManager() override;

    QList<PinWindow*> pins() const { return m_pins; }
    const QList<ClosedPin>& closedPins() const { return m_closed; }
    bool allHidden() const { return m_hidden; }

    PinWindow* pinImage(const QImage& image, QPoint physicalTopLeft);
    PinWindow* pinText(const QString& text, QPoint physicalTopLeft);
    // 剪贴板：图像 → 图像贴图；图片文件 → 每个文件一个贴图；文本 → 文本贴图。没有可贴内容返回 nullptr
    PinWindow* pinClipboard(QPoint physicalTopLeft);
    // 贴图热键：剪贴板自上次贴图后有变化就贴剪贴板，否则恢复最近关闭的贴图
    PinWindow* pinHotkey(QPoint physicalTopLeft);
    PinWindow* restoreLastClosed();

    void hideAll();
    void showAll();
    void toggleHidden();
    void closeAll();
    void destroyAll();
    void restoreOffscreen(); // PIN-55
    // 切换鼠标下方贴图的穿透；下方没有贴图时取消全部穿透（PIN-33）
    void togglePassthroughAt(QPoint physical);

    // 测试：模拟剪贴板变化
    void markClipboardDirty() { m_clipboardDirty = true; }
    bool clipboardDirty() const { return m_clipboardDirty; }

signals:
    void pinsChanged();

private:
    PinWindow* attach(PinWindow* pin, QPoint physicalTopLeft, const QDateTime& created);
    void onClosed(PinWindow* pin);
    void onDestroyed(PinWindow* pin);
    void trimClosed();

    Settings& m_settings;
    OutputActions* m_output;
    QList<PinWindow*> m_pins;
    QList<ClosedPin> m_closed;
    QHash<PinWindow*, QDateTime> m_created;
    bool m_hidden = false;
    bool m_clipboardDirty = true;
};

} // namespace ss
