#include "app/GroupDialog.h"

#include "app/WindowChrome.h"
#include "app/PinManager.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace ss {

GroupDialog::GroupDialog(PinManager& manager, QWidget* parent)
    : QDialog(parent)
    , m_manager(manager)
    , m_list(new QListWidget(this))
{
    setWindowTitle(tr("Pin groups"));
    resize(360, 320);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_list);
    auto* row = new QHBoxLayout;
    struct Btn {
        const char* name;
        QString text;
        void (GroupDialog::*slot)();
    };
    auto add = [&](const QString& name, const QString& text, auto slot) {
        auto* b = new QPushButton(text, this);
        b->setObjectName(name);
        connect(b, &QPushButton::clicked, this, slot);
        row->addWidget(b);
    };
    add(QStringLiteral("add"), tr("Add"), [this] { addGroup(); });
    add(QStringLiteral("remove"), tr("Remove"), [this] { removeCurrent(); });
    add(QStringLiteral("rename"), tr("Rename"), [this] { renameCurrent(); });
    add(QStringLiteral("up"), tr("Up"), [this] { moveCurrent(-1); });
    add(QStringLiteral("down"), tr("Down"), [this] { moveCurrent(1); });
    add(QStringLiteral("color"), tr("Color"), [this] { colorCurrent(); });
    layout->addLayout(row);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    layout->addWidget(buttons);
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) { m_manager.switchToGroup(m_list->row(item)); });
    refresh();
    theme::decorateWindow(this);
}

int GroupDialog::currentIndex() const
{
    return m_list->currentRow();
}

void GroupDialog::refresh()
{
    const int current = m_list->currentRow();
    m_list->clear();
    const QStringList names = m_manager.groupNames();
    for (int i = 0; i < names.size(); ++i) {
        const auto [onScreen, total] = m_manager.groupCounts(i);
        auto* item = new QListWidgetItem(QStringLiteral("%1  (%2 / %3)").arg(names[i]).arg(onScreen).arg(total), m_list);
        const QColor c = m_manager.groupColor(i);
        if (c.isValid())
            item->setForeground(c);
        if (i == m_manager.currentGroupIndex())
            item->setText(item->text() + QStringLiteral("  ◀"));
    }
    m_list->setCurrentRow(std::clamp(current, 0, static_cast<int>(names.size()) - 1));
}

void GroupDialog::addGroup()
{
    bool ok = false;
    const QString name = theme::getText(this, tr("New pin group"), tr("Group name:"), QString(), &ok);
    if (ok)
        m_manager.newGroup(name);
    refresh();
}

void GroupDialog::removeCurrent()
{
    const int index = currentIndex();
    if (index < 0 || m_manager.groupNames().size() <= 1)
        return;
    const QStringList names = m_manager.groupNames();
    QStringList others;
    for (int i = 0; i < names.size(); ++i)
        if (i != index)
            others.append(names[i]);
    bool ok = false;
    const QString target = theme::getItem(this, tr("Remove group"), tr("Move its pins to:"), others, 0, &ok);
    if (!ok)
        return;
    m_manager.removeGroup(index, static_cast<int>(names.indexOf(target)));
    refresh();
}

void GroupDialog::renameCurrent()
{
    const int index = currentIndex();
    if (index < 0)
        return;
    bool ok = false;
    const QString name = theme::getText(this, tr("Rename group"), tr("Group name:"), m_manager.groupNames().value(index), &ok);
    if (ok && !name.trimmed().isEmpty())
        m_manager.renameGroup(index, name.trimmed());
    refresh();
}

void GroupDialog::moveCurrent(int delta)
{
    const int index = currentIndex();
    if (index < 0)
        return;
    if (m_manager.moveGroup(index, index + delta)) {
        refresh();
        m_list->setCurrentRow(index + delta);
    }
}

void GroupDialog::colorCurrent()
{
    const int index = currentIndex();
    if (index < 0)
        return;
    const QColor c = theme::getColor(m_manager.groupColor(index), this, tr("Group color"));
    if (c.isValid())
        m_manager.setGroupColor(index, c);
    refresh();
}

} // namespace ss
