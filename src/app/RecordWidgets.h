#pragma once

#include "core/annotation/Annotation.h"

#include <QLabel>
#include <QPoint>
#include <QRect>
#include <QToolButton>
#include <QWidget>

namespace ss {

class RecordSession;
class AnnotationDocument;
class PencilAnnotation;

// 录制区域的物理矩形 → 逻辑几何（所在显示器的缩放）
struct RecordScreenMap {
    qreal scale = 1.0;
    QPoint logicalOrigin;
    QPoint physicalOrigin;
    QRect toLogical(const QRect& physical) const;
    QPoint toPhysical(QPoint logical) const;
};

// 录制区域边框（不参与捕获）
class RecordFrameWindow : public QWidget {
public:
    RecordFrameWindow(const QRect& physical, const RecordScreenMap& map);
    void setRecording(bool on);
    void showExcluded();

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QRect m_physical;
    bool m_recording = false;
};

// 录制工具栏（VID-01 / VID-02 / VID-04 / VID-05 / VID-07）：开始、暂停 / 继续、停止、重录、计时、延迟、模式、帧率、画质、声音、标注、摄像头
class RecordToolbar : public QWidget {
    Q_OBJECT
public:
    explicit RecordToolbar(RecordSession* session);
    void refresh();
    void showExcluded();
    QToolButton* button(const QString& name) const { return m_buttons.value(name); }

private:
    RecordSession* m_session;
    QLabel* m_time = nullptr;
    QHash<QString, QToolButton*> m_buttons;
    QWidget* m_options = nullptr;
};

// 录制中标注层（VID-05）：覆盖在区域上的透明窗口；标注模式下接收鼠标画笔画，否则穿透
class RecordAnnotateWindow : public QWidget {
public:
    RecordAnnotateWindow(const QRect& physical, const RecordScreenMap& map, AnnotationDocument* document);
    ~RecordAnnotateWindow() override;
    void setActive(bool on);
    void showExcluded();
    AnnotationStyle style;

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    QPointF toRegion(QPointF logical) const;
    QRect m_physical;
    RecordScreenMap m_map;
    AnnotationDocument* m_document;
    std::unique_ptr<PencilAnnotation> m_current;
    bool m_active = false;
};

} // namespace ss
