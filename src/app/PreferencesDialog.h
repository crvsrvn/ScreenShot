#pragma once

#include <QDialog>
#include <QHash>

namespace ss {

class Settings;

// 偏好设置窗口（CFG-01）：各页由设置表自动生成；每个选项悬停显示说明（CFG-04）；修改立即生效（CFG-07）。
class PreferencesDialog : public QDialog {
    Q_OBJECT
public:
    explicit PreferencesDialog(Settings& settings, QWidget* parent = nullptr);

    // 页标识 → 显示名
    static QString pageTitle(const QString& page);

private:
    void buildPages();
    QWidget* makeEditor(const struct SettingSpec& spec);

    Settings& m_settings;
};

} // namespace ss
