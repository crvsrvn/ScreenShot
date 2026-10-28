#pragma once

#include "app/AnnotationEditor.h"
#include "app/ToolbarTarget.h"

#include <QHash>
#include <QWidget>

class QToolButton;
class QSpinBox;
class QSlider;
class QFontComboBox;
class QComboBox;

namespace ss {

// 标注工具栏（ANN-01）：第一行是常用标注工具 + “更多” + 输出按钮（撤销 / 重做只用 Ctrl+Z / Ctrl+Y），
// 其余工具和低频操作（清除、模式、比例、阴影 / 边框等）收在“更多”弹出面板里；
// 第二行是当前工具的选项（颜色、笔宽、填充、形状、强度、字体），没有工具时隐藏。矩形 / 椭圆合为一个框选工具，箭头 / 直线合为一个箭头工具，都在形状选项里切换。
// 独立的置顶工具窗口，浮在覆盖窗口 / 贴图之上，不夺取焦点。宿主经 ToolbarTarget 接入。
class CaptureToolbar : public QWidget {
    Q_OBJECT
public:
    explicit CaptureToolbar(ToolbarTarget* target);

    QToolButton* button(const QString& name) const; // 工具名或 copy / save / pin / cancel / clear（含“更多”面板里的按钮）
    QWidget* optionsRow() const { return m_options; }
    void syncFromEditor();
    static QString toolName(AnnotationEditor::Tool tool);
    static AnnotationEditor::Tool toolFromName(const QString& name);
    static const char* kToolText(AnnotationEditor::Tool tool);
    // 色板（ANN-04）
    QList<QColor> paletteColors() const;
    void savePalette(const QList<QColor>& colors);
    bool exportPalette(const QString& path) const;
    bool importPalette(const QString& path);
    QList<QToolButton*> colorButtons() const { return m_colorButtons; }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QToolButton* addButton(QWidget* row, const QString& name, const QString& text, const QString& tooltip, bool checkable = false);
    void buildOptions();
    void refreshColorButtons();
    void rebuildPalette();
    void rebuildTools();

    ToolbarTarget* m_target;
    QWidget* m_tools = nullptr;
    QWidget* m_moreTools = nullptr;   // “更多”面板第一行：不在常用区的工具
    QWidget* m_moreActions = nullptr; // “更多”面板第二行：低频操作
    QWidget* m_options = nullptr;
    QWidget* m_fillRow = nullptr;
    QWidget* m_shapeRow = nullptr;
    QWidget* m_strengthRow = nullptr;
    QWidget* m_textRow = nullptr;
    QWidget* m_styleRow = nullptr;
    QWidget* m_arrowRow = nullptr;
    QWidget* m_stepRow = nullptr;
    QWidget* m_magnifierRow = nullptr;
    QSpinBox* m_radius = nullptr;
    QComboBox* m_arrowStart = nullptr;
    QComboBox* m_arrowEnd = nullptr;
    QComboBox* m_stepStyle = nullptr;
    QSpinBox* m_stepValue = nullptr;
    QSpinBox* m_zoom = nullptr;
    QSpinBox* m_width = nullptr;
    QSlider* m_strength = nullptr;
    QFontComboBox* m_fontCombo = nullptr;
    QSpinBox* m_fontSize = nullptr;
    QToolButton* m_customColor = nullptr;
    QWidget* m_paletteRow = nullptr;
    QToolButton* m_recentButton = nullptr;
    QToolButton* m_moreButton = nullptr;
    AnnotationEditor::Tool m_recentTool = AnnotationEditor::Tool::None;
    QList<QToolButton*> m_colorButtons;
    QHash<QString, QToolButton*> m_toolButtons;
    bool m_syncing = false;
};

} // namespace ss
