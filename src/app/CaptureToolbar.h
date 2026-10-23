#pragma once

#include <QWidget>

class QToolButton;

namespace ss {

class CaptureSession;

// 选区工具栏（ANN-01 的容器；M1 只有输出按钮，M2 加标注工具）。
// 独立的置顶工具窗口，浮在覆盖窗口之上。
class CaptureToolbar : public QWidget {
    Q_OBJECT
public:
    explicit CaptureToolbar(CaptureSession* session);

    QToolButton* button(const QString& name) const; // copy / save / quickSave / pin / cancel

private:
    QToolButton* addButton(const QString& name, const QString& text, const QString& tooltip);

    CaptureSession* m_session;
};

} // namespace ss
