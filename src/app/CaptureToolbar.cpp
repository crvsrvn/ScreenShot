#include "app/CaptureToolbar.h"

#include "app/CaptureSession.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QToolButton>

namespace ss {

CaptureToolbar::CaptureToolbar(CaptureSession* session)
    : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus)
    , m_session(session)
{
    setAttribute(Qt::WA_ShowWithoutActivating);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(2);
    setStyleSheet(QStringLiteral("QWidget { background: #2b2b2b; } QToolButton { color: white; padding: 4px 8px; border: none; } QToolButton:hover { background: #3d6fb4; }"));
    connect(addButton(QStringLiteral("copy"), tr("Copy"), tr("Copy to clipboard (Enter)")), &QToolButton::clicked, session, &CaptureSession::copy);
    connect(addButton(QStringLiteral("save"), tr("Save"), tr("Save to file (Ctrl+S); Shift+click for quick save")), &QToolButton::clicked, this, [this] {
        if (QApplication::keyboardModifiers() & Qt::ShiftModifier)
            m_session->quickSave();
        else
            m_session->save();
    });
    connect(addButton(QStringLiteral("quickSave"), tr("Quick save"), tr("Save to the quick save folder (Ctrl+Shift+S)")), &QToolButton::clicked, session, &CaptureSession::quickSave);
    connect(addButton(QStringLiteral("pin"), tr("Pin"), tr("Pin to screen (Ctrl+T)")), &QToolButton::clicked, session, &CaptureSession::pin);
    connect(addButton(QStringLiteral("cancel"), tr("Cancel"), tr("Cancel (Esc)")), &QToolButton::clicked, session, &CaptureSession::cancel);
}

QToolButton* CaptureToolbar::addButton(const QString& name, const QString& text, const QString& tooltip)
{
    auto* b = new QToolButton(this);
    b->setObjectName(name);
    b->setText(text);
    b->setToolTip(tooltip);
    b->setFocusPolicy(Qt::NoFocus);
    layout()->addWidget(b);
    return b;
}

QToolButton* CaptureToolbar::button(const QString& name) const
{
    return findChild<QToolButton*>(name);
}

} // namespace ss
