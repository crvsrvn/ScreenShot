#include "app/ActionsDialog.h"

#include "app/WindowChrome.h"
#include "core/config/Settings.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace ss {

ActionsDialog::ActionsDialog(Settings& settings, QWidget* parent)
    : QDialog(parent)
    , m_settings(settings)
    , m_table(new QTableWidget(0, 4, this))
{
    setWindowTitle(tr("Actions"));
    resize(640, 360);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("Commands: snip [...], pin [...], pins ..., group ..., prefs, quit; shell <command line>; run <program> [args]"), this));
    m_table->setHorizontalHeaderLabels({tr("Name"), tr("Command"), tr("Hotkey"), tr("In tray menu")});
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(m_table);
    auto* row = new QHBoxLayout;
    auto* add = new QPushButton(tr("Add"), this);
    add->setObjectName(QStringLiteral("add"));
    connect(add, &QPushButton::clicked, this, [this] { addRow(); });
    auto* remove = new QPushButton(tr("Remove"), this);
    remove->setObjectName(QStringLiteral("remove"));
    connect(remove, &QPushButton::clicked, this, &ActionsDialog::removeCurrentRow);
    row->addWidget(add);
    row->addWidget(remove);
    row->addStretch();
    layout->addLayout(row);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        apply();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    setActions(actionsFromJson(settings.stringValue(QStringLiteral("actions.list"))));
    theme::decorateWindow(this);
}

void ActionsDialog::addRow(const Action& a)
{
    const int r = m_table->rowCount();
    m_table->insertRow(r);
    m_table->setItem(r, 0, new QTableWidgetItem(a.name));
    m_table->setItem(r, 1, new QTableWidgetItem(a.command));
    auto* keys = new QKeySequenceEdit(QKeySequence(a.hotkey), m_table);
    m_table->setCellWidget(r, 2, keys);
    auto* tray = new QCheckBox(m_table);
    tray->setChecked(a.inTray);
    m_table->setCellWidget(r, 3, tray);
}

void ActionsDialog::removeCurrentRow()
{
    if (m_table->currentRow() >= 0)
        m_table->removeRow(m_table->currentRow());
}

void ActionsDialog::setActions(const QList<Action>& actions)
{
    m_table->setRowCount(0);
    for (const Action& a : actions)
        addRow(a);
}

QList<Action> ActionsDialog::actions() const
{
    QList<Action> out;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        Action a;
        a.name = m_table->item(r, 0) ? m_table->item(r, 0)->text().trimmed() : QString();
        a.command = m_table->item(r, 1) ? m_table->item(r, 1)->text().trimmed() : QString();
        if (auto* keys = qobject_cast<QKeySequenceEdit*>(m_table->cellWidget(r, 2)))
            a.hotkey = keys->keySequence().toString();
        if (auto* tray = qobject_cast<QCheckBox*>(m_table->cellWidget(r, 3)))
            a.inTray = tray->isChecked();
        if (!a.name.isEmpty() && !a.command.isEmpty())
            out.append(a);
    }
    return out;
}

void ActionsDialog::apply()
{
    m_settings.setValue(QStringLiteral("actions.list"), actionsToJson(actions()));
}

} // namespace ss
