#pragma once

#include <QDialog>

class QListWidget;

namespace ss {

class PinManager;

// 组管理器（GRP-02）：增删、改名、排序、颜色标签；删除组时把贴图迁到其他组
class GroupDialog : public QDialog {
    Q_OBJECT
public:
    explicit GroupDialog(PinManager& manager, QWidget* parent = nullptr);

    void addGroup();
    void removeCurrent();
    void renameCurrent();
    void moveCurrent(int delta);
    void colorCurrent();
    int currentIndex() const;

private:
    void refresh();
    PinManager& m_manager;
    QListWidget* m_list;
};

} // namespace ss
