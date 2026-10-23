#pragma once

namespace ss::mac {

// 权限检测与申请（GEN-14、NFR-16）。只申请屏幕录制和辅助功能两项。
bool hasScreenRecordingPermission();
// 弹出系统申请对话框；返回当前是否已授权（用户可能稍后才授权）
bool requestScreenRecordingPermission();

bool hasAccessibilityPermission();
// prompt=true 时弹出系统提示
bool requestAccessibilityPermission(bool prompt);

// 打开系统设置中对应的隐私页面
void openScreenRecordingSettings();
void openAccessibilitySettings();

} // namespace ss::mac
