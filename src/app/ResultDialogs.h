#pragma once

#include "core/code/CodeDecoder.h"

#include <QDialog>
#include <QList>
#include <QString>

class QPlainTextEdit;
class QListWidget;

namespace ss {

// OCR 结果窗口（REC-01）：可编辑、可复制
class OcrResultDialog : public QDialog {
    Q_OBJECT
public:
    explicit OcrResultDialog(const QString& text, QWidget* parent = nullptr);
    QString text() const;
    void copyAll();

private:
    QPlainTextEdit* m_edit;
};

// 二维码 / 条码结果窗口（REC-06）：多个码以列表显示；网址可交给系统浏览器打开
class CodeResultDialog : public QDialog {
    Q_OBJECT
public:
    explicit CodeResultDialog(const QList<CodeResult>& codes, QWidget* parent = nullptr);
    int count() const { return static_cast<int>(m_codes.size()); }
    QString selectedText() const;
    bool selectedIsUrl() const;
    void copySelected();
    void openSelected(); // 只对 http(s) 网址有效

private:
    QList<CodeResult> m_codes;
    QListWidget* m_list;
};

} // namespace ss
