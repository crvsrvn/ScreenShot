#include "app/ResultDialogs.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace ss {

OcrResultDialog::OcrResultDialog(const QString& text, QWidget* parent)
    : QDialog(parent)
    , m_edit(new QPlainTextEdit(this))
{
    setWindowTitle(tr("Recognized text"));
    resize(480, 360);
    auto* layout = new QVBoxLayout(this);
    m_edit->setPlainText(text);
    layout->addWidget(m_edit);
    auto* buttons = new QDialogButtonBox(this);
    QPushButton* copy = buttons->addButton(tr("Copy"), QDialogButtonBox::ActionRole);
    connect(copy, &QPushButton::clicked, this, &OcrResultDialog::copyAll);
    buttons->addButton(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    layout->addWidget(buttons);
}

QString OcrResultDialog::text() const
{
    return m_edit->toPlainText();
}

void OcrResultDialog::copyAll()
{
    QApplication::clipboard()->setText(text());
}

CodeResultDialog::CodeResultDialog(const QList<CodeResult>& codes, QWidget* parent)
    : QDialog(parent)
    , m_codes(codes)
    , m_list(new QListWidget(this))
{
    setWindowTitle(tr("Codes found: %1").arg(codes.size()));
    resize(480, 300);
    auto* layout = new QVBoxLayout(this);
    for (const CodeResult& c : codes)
        m_list->addItem(QStringLiteral("[%1] %2").arg(c.format, c.text));
    if (!codes.isEmpty())
        m_list->setCurrentRow(0);
    layout->addWidget(m_list);
    auto* buttons = new QDialogButtonBox(this);
    QPushButton* copy = buttons->addButton(tr("Copy"), QDialogButtonBox::ActionRole);
    connect(copy, &QPushButton::clicked, this, &CodeResultDialog::copySelected);
    QPushButton* open = buttons->addButton(tr("Open in browser"), QDialogButtonBox::ActionRole);
    open->setObjectName(QStringLiteral("open"));
    connect(open, &QPushButton::clicked, this, &CodeResultDialog::openSelected);
    connect(m_list, &QListWidget::currentRowChanged, this, [this, open] { open->setEnabled(selectedIsUrl()); });
    open->setEnabled(selectedIsUrl());
    buttons->addButton(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    layout->addWidget(buttons);
}

QString CodeResultDialog::selectedText() const
{
    const int row = m_list->currentRow();
    return row >= 0 && row < m_codes.size() ? m_codes[row].text : QString();
}

bool CodeResultDialog::selectedIsUrl() const
{
    const int row = m_list->currentRow();
    return row >= 0 && row < m_codes.size() && m_codes[row].isUrl();
}

void CodeResultDialog::copySelected()
{
    QApplication::clipboard()->setText(selectedText());
}

void CodeResultDialog::openSelected()
{
    if (selectedIsUrl())
        QDesktopServices::openUrl(QUrl(selectedText())); // 交给系统浏览器，程序本身不联网
}

} // namespace ss
