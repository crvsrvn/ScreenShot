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
    virtual bool supportsScroll() const { return false; } // 截图会话支持“滚动截图”
    virtual void startScroll() {}
    // 截图会话的输出选项（CAP-18 固定比例、CAP-33 阴影 / 边框）；贴图宿主不提供
    virtual bool supportsCaptureOptions() const { return false; }
    virtual bool ratioLocked() const { return false; }
    virtual void setRatioLocked(bool) {}
    virtual bool shadowEnabled() const { return false; }
    virtual void setShadowEnabled(bool) {}
    virtual bool borderEnabled() const { return false; }
    virtual void setBorderEnabled(bool) {}
    virtual void print() {}
    // 二维码 / 条码（REC-06）：截图会话在手动模式提供按钮，自动模式检测到码后显示按钮
    virtual void detectCodes() {}
    // 模式轮换（CAP-07）：截图会话提供
    virtual QString modeName() const { return QString(); }
    // 模式按钮的图标名（modeStatic / modeScroll / modeRecord）
    virtual QString modeIconName() const { return QStringLiteral("modeStatic"); }
    virtual void cycleMode() {}
    virtual bool hasCodes() const { return false; }
};

} // namespace ss
