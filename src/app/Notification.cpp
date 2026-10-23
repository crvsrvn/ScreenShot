#include "app/Notification.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMimeData>
#include <QProcess>
#include <QPushButton>
#include <QScreen>
#include <QUrl>
#include <QVBoxLayout>

namespace ss {

std::function<void(const QString&)> NotificationWidget::revealHandler = [](const QString& path) {
#if defined(Q_OS_WIN)
    QProcess::startDetached(QStringLiteral("explorer.exe"), {QStringLiteral("/select,"), QDir::toNativeSeparators(path)});
#elif defined(Q_OS_MACOS)
    QProcess::startDetached(QStringLiteral("open"), {QStringLiteral("-R"), path});
#else
    Q_UNUSED(path);
#endif
};

NotificationWidget::NotificationWidget(const QString& title, const QString& message, const QString& filePath, int timeoutMs)
    : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus)
    , m_path(filePath)
{
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_DeleteOnClose);
    setObjectName(QStringLiteral("notification"));
    setStyleSheet(QStringLiteral("QWidget#notification { background: #2b2b2b; border: 1px solid #555; border-radius: 6px; } QLabel { color: white; } "
                                 "QPushButton { color: white; background: #3a3a3a; border: 1px solid #666; border-radius: 3px; padding: 3px 8px; } QPushButton:hover { background: #3d6fb4; }"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 10, 12, 10);
    auto* titleLabel = new QLabel(QStringLiteral("<b>%1</b>").arg(title.toHtmlEscaped()), this);
    layout->addWidget(titleLabel);
    auto* messageLabel = new QLabel(message, this);
    messageLabel->setObjectName(QStringLiteral("message"));
    messageLabel->setWordWrap(true);
    messageLabel->setMaximumWidth(360);
    layout->addWidget(messageLabel);
    if (!filePath.isEmpty()) {
        auto* row = new QHBoxLayout;
        auto* reveal = new QPushButton(tr("Show in folder"), this);
        reveal->setObjectName(QStringLiteral("reveal"));
        connect(reveal, &QPushButton::clicked, this, &NotificationWidget::revealFile);
        auto* copyFile = new QPushButton(tr("Copy file"), this);
        copyFile->setObjectName(QStringLiteral("copyFile"));
        connect(copyFile, &QPushButton::clicked, this, &NotificationWidget::copyFile);
        auto* copyPath = new QPushButton(tr("Copy path"), this);
        copyPath->setObjectName(QStringLiteral("copyPath"));
        connect(copyPath, &QPushButton::clicked, this, &NotificationWidget::copyPath);
        row->addWidget(reveal);
        row->addWidget(copyFile);
        row->addWidget(copyPath);
        row->addStretch();
        layout->addLayout(row);
    }
    m_timer.setSingleShot(true);
    m_timer.setInterval(timeoutMs);
    connect(&m_timer, &QTimer::timeout, this, &QWidget::close);
    adjustSize();
}

void NotificationWidget::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    // 主屏右上角
    if (QScreen* s = QGuiApplication::primaryScreen()) {
        const QRect avail = s->availableGeometry();
        move(avail.right() - width() - 16, avail.top() + 16);
    }
    m_timer.start();
}

void NotificationWidget::mousePressEvent(QMouseEvent*)
{
    close(); // 点击正文关闭
}

void NotificationWidget::revealFile()
{
    revealHandler(m_path);
    close();
}

void NotificationWidget::copyFile()
{
    auto* mime = new QMimeData;
    mime->setUrls({QUrl::fromLocalFile(m_path)});
    mime->setText(QDir::toNativeSeparators(m_path));
    QApplication::clipboard()->setMimeData(mime);
    close();
}

void NotificationWidget::copyPath()
{
    QApplication::clipboard()->setText(QDir::toNativeSeparators(m_path));
    close();
}

} // namespace ss
