#pragma once

#include "app/PreferencesWidgets.h"

#include <QDialog>
#include <QHash>

class QLabel;
class QListWidget;
class QStackedWidget;

namespace ss {

class Settings;

// 偏好设置窗口（CFG-01）：左侧分类与搜索，右侧卡片式选项，各页由设置表自动生成；
// 每个选项显示说明，悬停也显示（CFG-04）；修改立即生效（CFG-07）。
class PreferencesDialog : public QDialog {
    Q_OBJECT
public:
    explicit PreferencesDialog(Settings& settings, QWidget* parent = nullptr);

    // 页标识 → 显示名
    static QString pageTitle(const QString& page);

private:
    struct Row {
        QWidget* widget;
        QString text; // 标题 + 说明，供搜索匹配
        int page;
    };

    void buildPages();
    QWidget* makeRow(const struct SettingSpec& spec, QWidget* parent);
    QWidget* makeEditor(const struct SettingSpec& spec);
    void filter(const QString& text);

    Settings& m_settings;
    prefs::Colors m_colors;
    QListWidget* m_nav = nullptr;
    QStackedWidget* m_stack = nullptr;
    QLabel* m_empty = nullptr;
    QList<Row> m_rows;
};

} // namespace ss
