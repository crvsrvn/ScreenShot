#pragma once

#include "app/AnnotationEditor.h"

#include <QHash>
#include <QWidget>

class QToolButton;
class QSpinBox;
class QSlider;
class QFontComboBox;

namespace ss {

class CaptureSession;

// 选区工具栏（ANN-01）：第一行是标注工具 + 撤销 / 重做 / 清除 + 输出按钮；
// 第二行是当前工具的选项（颜色、笔宽、填充、形状、强度、字体），没有工具时隐藏。
// 独立的置顶工具窗口，浮在覆盖窗口之上，不夺取焦点。
class CaptureToolbar : public QWidget {
    Q_OBJECT
public:
    explicit CaptureToolbar(CaptureSession* session);

    QToolButton* button(const QString& name) const; // 工具名或 copy / save / quickSave / pin / cancel / undo / redo / clear
    QWidget* optionsRow() const { return m_options; }
    static QString toolName(AnnotationEditor::Tool tool);
    static AnnotationEditor::Tool toolFromName(const QString& name);

private:
    QToolButton* addButton(QWidget* row, const QString& name, const QString& text, const QString& tooltip, bool checkable = false);
    void buildOptions();
    void syncFromEditor();
    void refreshColorButtons();

    CaptureSession* m_session;
    QWidget* m_tools = nullptr;
    QWidget* m_options = nullptr;
    QWidget* m_fillRow = nullptr;
    QWidget* m_shapeRow = nullptr;
    QWidget* m_strengthRow = nullptr;
    QWidget* m_textRow = nullptr;
    QSpinBox* m_width = nullptr;
    QSlider* m_strength = nullptr;
    QFontComboBox* m_fontCombo = nullptr;
    QSpinBox* m_fontSize = nullptr;
    QToolButton* m_customColor = nullptr;
    QList<QToolButton*> m_colorButtons;
    QHash<QString, QToolButton*> m_toolButtons;
    bool m_syncing = false;
};

} // namespace ss
