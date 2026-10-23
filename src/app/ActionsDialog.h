#pragma once

#include "core/config/Actions.h"

#include <QDialog>

class QTableWidget;

namespace ss {

class Settings;

// 动作编辑（ACT-03）：名称 / 命令 / 热键 / 托盘菜单；确定后写入 actions.list
class ActionsDialog : public QDialog {
    Q_OBJECT
public:
    explicit ActionsDialog(Settings& settings, QWidget* parent = nullptr);
    QList<Action> actions() const;
    void setActions(const QList<Action>& actions);
    void addRow(const Action& action = {});
    void removeCurrentRow();
    void apply(); // 写入设置

private:
    Settings& m_settings;
    QTableWidget* m_table;
};

} // namespace ss
