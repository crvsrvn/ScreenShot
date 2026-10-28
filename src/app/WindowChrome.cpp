#include "app/WindowChrome.h"

#include "app/Theme.h"

#include <QAbstractButton>
#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWindow>

namespace ss::theme {

namespace {

bool resizable(const QWidget* w)
{
    return w->minimumSize() != w->maximumSize();
}

void toggleMaximized(QWidget* w)
{
    if (w->isMaximized())
        w->showNormal();
    else
        w->showMaximized();
}

// 标题栏按钮：46×32，细线图标；关闭按钮悬停为红底白字（WinUI 标题栏按钮的配色）
class CaptionButton : public QAbstractButton {
public:
    enum Kind { Minimize, Maximize, Close };

    CaptionButton(Kind kind, QWidget* parent)
        : QAbstractButton(parent)
        , m_kind(kind)
    {
        setFixedSize(46, 32);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_Hover);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        const Colors& c = colors();
        QPainter p(this);
        const bool hover = underMouse();
        const bool down = isDown();
        QColor glyph = c.controlText;
        if (m_kind == Close && (hover || down)) {
            QColor bg = c.closeHover;
            if (down)
                bg.setAlpha(230);
            p.fillRect(rect(), bg);
            glyph = down ? QColor(255, 255, 255, 180) : Qt::white;
        } else if (hover || down) {
            p.fillRect(rect(), c.dark ? QColor(255, 255, 255, down ? 15 : 25) : QColor(0, 0, 0, down ? 15 : 25));
        }
        if (!window()->isActiveWindow() && !hover)
            glyph = c.disabledText;
        // 图标 10×10，居中，按物理像素对齐以保持 1px 细线清晰
        const QRectF box(width() / 2.0 - 5, height() / 2.0 - 5, 10, 10);
        p.setPen(QPen(glyph, 1));
        p.setBrush(Qt::NoBrush);
        switch (m_kind) {
        case Minimize:
            p.drawLine(QPointF(box.left(), box.center().y() + 0.5), QPointF(box.right(), box.center().y() + 0.5));
            break;
        case Maximize:
            if (window()->isMaximized()) {
                p.drawRect(box.adjusted(0.5, 2.5, -2.5, -0.5));
                p.drawPolyline(QPolygonF({box.topLeft() + QPointF(2.5, 2.5), box.topLeft() + QPointF(2.5, 0.5), box.topRight() + QPointF(-0.5, 0.5),
                                          box.bottomRight() + QPointF(-0.5, -2.5), box.bottomRight() + QPointF(-2.5, -2.5)}));
            } else {
                p.drawRect(box.adjusted(0.5, 0.5, -0.5, -0.5));
            }
            break;
        case Close:
            p.setRenderHint(QPainter::Antialiasing);
            p.drawLine(box.topLeft(), box.bottomRight());
            p.drawLine(box.topRight(), box.bottomLeft());
            break;
        }
    }

private:
    Kind m_kind;
};

// 标题栏：图标、标题，右侧标题栏按钮；拖动移动窗口，双击最大化
class TitleBar : public QWidget {
public:
    explicit TitleBar(QWidget* window)
        : QWidget(window)
        , m_window(window)
    {
        setObjectName(QStringLiteral("titleBar"));
        const Qt::WindowFlags flags = window->windowFlags();
        auto* row = new QHBoxLayout(this);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(0);
        row->addStretch(1);
        if (flags & Qt::WindowMinimizeButtonHint) {
            auto* b = new CaptionButton(CaptionButton::Minimize, this);
            b->setObjectName(QStringLiteral("titleMinimize"));
            connect(b, &QAbstractButton::clicked, window, &QWidget::showMinimized);
            row->addWidget(b, 0, Qt::AlignTop);
        }
        if (flags & Qt::WindowMaximizeButtonHint) {
            m_maximize = new CaptionButton(CaptionButton::Maximize, this);
            m_maximize->setObjectName(QStringLiteral("titleMaximize"));
            connect(m_maximize, &QAbstractButton::clicked, window, [window] { toggleMaximized(window); });
            row->addWidget(m_maximize, 0, Qt::AlignTop);
        }
        auto* close = new CaptionButton(CaptionButton::Close, this);
        close->setObjectName(QStringLiteral("titleClose"));
        connect(close, &QAbstractButton::clicked, window, &QWidget::close);
        row->addWidget(close, 0, Qt::AlignTop);
    }

    QAbstractButton* maximizeButton() const { return m_maximize; }

protected:
    void paintEvent(QPaintEvent*) override
    {
        const Colors& c = colors();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        // 图标：设置了窗口图标就用它，否则画一个金色取景框徽标
        const QRectF logo(16, (height() - 28) / 2.0, 28, 28);
        const QIcon icon = m_window->windowIcon();
        if (!QApplication::windowIcon().isNull() && !icon.isNull()) {
            icon.paint(&p, logo.toRect());
        } else {
            p.setPen(QPen(c.border, 1));
            p.setBrush(gradient(logo, c.accent, 2));
            p.drawRoundedRect(logo.adjusted(0.5, 0.5, -0.5, -0.5), 7, 7);
            p.setPen(QPen(c.onAccent, 2));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(logo.adjusted(7, 7, -7, -7), 3, 3);
            p.setPen(Qt::NoPen);
            p.setBrush(c.onAccent);
            p.drawEllipse(logo.center(), 2.5, 2.5);
        }
        QFont f = font();
        f.setWeight(QFont::DemiBold);
        p.setFont(f);
        p.setPen(m_window->isActiveWindow() ? c.text : c.muted);
        const int right = width() - (layout()->count() - 1) * 46 - 12;
        const QRect textRect(static_cast<int>(logo.right()) + 10, 0, std::max(0, right - static_cast<int>(logo.right()) - 10), height());
        p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, QFontMetrics(f).elidedText(m_window->windowTitle(), Qt::ElideRight, textRect.width()));
    }

    void mousePressEvent(QMouseEvent* e) override
    {
        if (e->button() == Qt::LeftButton && m_window->windowHandle())
            m_window->windowHandle()->startSystemMove();
    }

    void mouseDoubleClickEvent(QMouseEvent* e) override
    {
        if (e->button() == Qt::LeftButton && m_maximize)
            toggleMaximized(m_window);
    }

private:
    QWidget* m_window;
    CaptionButton* m_maximize = nullptr;
};

// 窗口边缘的缩放手柄（透明），按下后交给系统缩放
class Grip : public QWidget {
public:
    Grip(Qt::Edges edges, QWidget* window)
        : QWidget(window)
        , m_edges(edges)
    {
        const bool horizontal = edges & (Qt::LeftEdge | Qt::RightEdge);
        const bool vertical = edges & (Qt::TopEdge | Qt::BottomEdge);
        if (horizontal && vertical)
            setCursor(edges == (Qt::TopEdge | Qt::LeftEdge) || edges == (Qt::BottomEdge | Qt::RightEdge) ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor);
        else
            setCursor(horizontal ? Qt::SizeHorCursor : Qt::SizeVerCursor);
    }

    Qt::Edges edges() const { return m_edges; }

protected:
    void mousePressEvent(QMouseEvent* e) override
    {
        if (e->button() == Qt::LeftButton && window()->windowHandle())
            window()->windowHandle()->startSystemResize(m_edges);
    }

private:
    Qt::Edges m_edges;
};

// 窗口描边（1px，与 Windows 10 窗口边框同色）；不接收鼠标
class Outline : public QWidget {
public:
    explicit Outline(QWidget* window)
        : QWidget(window)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setPen(QColor(0x70, 0x70, 0x70));
        p.drawRect(rect().adjusted(0, 0, -1, -1));
    }
};

// 跟随窗口：布局让出标题栏、摆放标题栏 / 描边 / 手柄、刷新标题与最大化状态
class Chrome : public QObject {
public:
    explicit Chrome(QWidget* window)
        : QObject(window)
        , m_window(window)
        , m_titleBar(new TitleBar(window))
        , m_outline(new Outline(window))
    {
        for (Qt::Edges e : {Qt::Edges(Qt::LeftEdge), Qt::Edges(Qt::RightEdge), Qt::Edges(Qt::TopEdge), Qt::Edges(Qt::BottomEdge),
                            Qt::TopEdge | Qt::LeftEdge, Qt::TopEdge | Qt::RightEdge, Qt::BottomEdge | Qt::LeftEdge, Qt::BottomEdge | Qt::RightEdge})
            m_grips.append(new Grip(e, window));
        window->installEventFilter(this);
        if (window->testAttribute(Qt::WA_WState_Polished))
            reserveTitleBar();
        relayout();
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched != m_window)
            return false;
        switch (event->type()) {
        case QEvent::Polish:
            reserveTitleBar();
            break;
        case QEvent::Show:
        case QEvent::Resize:
        case QEvent::WindowStateChange:
            relayout();
            break;
        case QEvent::WindowTitleChange:
        case QEvent::ActivationChange:
            m_titleBar->update();
            break;
        default:
            break;
        }
        return false;
    }

private:
    void reserveTitleBar()
    {
        if (m_reserved || !m_window->layout())
            return;
        m_reserved = true;
        QMargins m = m_window->layout()->contentsMargins();
        m.setTop(m.top() + kTitleBarHeight);
        m_window->layout()->setContentsMargins(m);
    }

    void relayout()
    {
        const QRect r = m_window->rect();
        const bool framed = !m_window->isMaximized() && !m_window->isFullScreen();
        m_titleBar->setGeometry(0, 0, r.width(), kTitleBarHeight);
        m_titleBar->raise();
        if (QAbstractButton* max = static_cast<TitleBar*>(m_titleBar)->maximizeButton())
            max->setVisible(resizable(m_window));
        m_outline->setGeometry(r);
        m_outline->setVisible(framed);
        m_outline->raise();
        const int g = 5; // 手柄厚度
        const bool canResize = framed && resizable(m_window);
        for (Grip* grip : m_grips) {
            const Qt::Edges e = grip->edges();
            const int x = e & Qt::LeftEdge ? 0 : e & Qt::RightEdge ? r.width() - g : g;
            const int y = e & Qt::TopEdge ? 0 : e & Qt::BottomEdge ? r.height() - g : g;
            const int w = e & (Qt::LeftEdge | Qt::RightEdge) ? g : r.width() - 2 * g;
            const int h = e & (Qt::TopEdge | Qt::BottomEdge) ? g : r.height() - 2 * g;
            grip->setGeometry(x, y, w, h);
            grip->setVisible(canResize);
            grip->raise();
        }
    }

    QWidget* m_window;
    QWidget* m_titleBar;
    QWidget* m_outline;
    QList<Grip*> m_grips;
    bool m_reserved = false;
};

} // namespace

void decorateWindow(QWidget* window)
{
    if (window->property("themeChrome").toBool())
        return;
    window->setProperty("themeChrome", true);
    window->setWindowFlags(window->windowFlags() | Qt::FramelessWindowHint);
    new Chrome(window);
}

QMessageBox::StandardButton message(QWidget* parent, QMessageBox::Icon icon, const QString& title, const QString& text,
                                    QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton)
{
    QMessageBox box(icon, title, text, buttons, parent);
    if (defaultButton != QMessageBox::NoButton)
        box.setDefaultButton(defaultButton);
    decorateWindow(&box);
    return static_cast<QMessageBox::StandardButton>(box.exec());
}

QString getText(QWidget* parent, const QString& title, const QString& label, const QString& text, bool* ok)
{
    QInputDialog d(parent);
    d.setWindowTitle(title);
    d.setLabelText(label);
    d.setTextValue(text);
    decorateWindow(&d);
    const bool accepted = d.exec() == QDialog::Accepted;
    if (ok)
        *ok = accepted;
    return accepted ? d.textValue() : QString();
}

QString getItem(QWidget* parent, const QString& title, const QString& label, const QStringList& items, int current, bool* ok)
{
    QInputDialog d(parent);
    d.setWindowTitle(title);
    d.setLabelText(label);
    d.setComboBoxItems(items);
    d.setComboBoxEditable(false);
    d.setTextValue(items.value(current));
    decorateWindow(&d);
    const bool accepted = d.exec() == QDialog::Accepted;
    if (ok)
        *ok = accepted;
    return accepted ? d.textValue() : QString();
}

QColor getColor(const QColor& initial, QWidget* parent, const QString& title, QColorDialog::ColorDialogOptions options)
{
    QColorDialog d(initial, parent);
    if (!title.isEmpty())
        d.setWindowTitle(title);
    d.setOptions(options | QColorDialog::DontUseNativeDialog);
    decorateWindow(&d);
    return d.exec() == QDialog::Accepted ? d.selectedColor() : QColor();
}

} // namespace ss::theme
