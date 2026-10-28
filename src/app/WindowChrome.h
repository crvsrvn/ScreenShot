#pragma once

#include <QColorDialog>
#include <QMessageBox>

class QWidget;

namespace ss::theme {

// 自绘标题栏（与 AITranslator 一致）：窗口改为无边框，顶部 48px 标题栏画图标、标题与最小化 / 最大化 / 关闭；
// 拖动标题栏移动、双击最大化，拖动窗口边缘缩放。须在窗口显示前调用，窗口布局的上边距自动让出标题栏。
void decorateWindow(QWidget* window);
constexpr int kTitleBarHeight = 48;

// 标准对话框的同风格版本（自绘标题栏），参数与 Qt 的静态函数一致
QMessageBox::StandardButton message(QWidget* parent, QMessageBox::Icon icon, const QString& title, const QString& text,
                                    QMessageBox::StandardButtons buttons = QMessageBox::Ok,
                                    QMessageBox::StandardButton defaultButton = QMessageBox::NoButton);
QString getText(QWidget* parent, const QString& title, const QString& label, const QString& text, bool* ok);
QString getItem(QWidget* parent, const QString& title, const QString& label, const QStringList& items, int current, bool* ok);
QColor getColor(const QColor& initial, QWidget* parent, const QString& title, QColorDialog::ColorDialogOptions options = {});

} // namespace ss::theme
