#pragma once

#include "app/AnnotationEditor.h"

namespace ss {

class Settings;

// 标注工具栏的宿主：截图会话（选区上）和贴图窗口（PIN-38）都实现它。
class ToolbarTarget {
public:
    virtual ~ToolbarTarget() = default;
    virtual AnnotationEditor& editor() = 0;
    virtual Settings& settings() = 0;
    virtual bool annotating() const = 0;      // 是否处于标注状态（决定工具按钮是否显示选中）
    virtual void setTool(AnnotationEditor::Tool tool) = 0;
    virtual void copy() = 0;
    virtual void save() = 0;
    virtual void quickSave() = 0;
    virtual void pin() = 0;                   // 贴图宿主上为“复制为新贴图”
    virtual void cancel() = 0;                // 截图：取消；贴图：关闭工具栏
    virtual bool supportsPin() const { return true; }
};

} // namespace ss
