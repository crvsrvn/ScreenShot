#include "app/PreferencesDialog.h"

#include "app/PreferencesWidgets.h"
#include "app/Theme.h"
#include "app/ToolbarIcons.h"
#include "app/WindowChrome.h"
#include "core/config/Settings.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace ss {

namespace {
QString tr2(const char* text)
{
    return QCoreApplication::translate("Settings", text);
}

// 侧边栏图标：复用工具栏的矢量图标
QIcon pageIcon(const QString& page, const QColor& color)
{
    static const QHash<QString, QString> icons{
        {QStringLiteral("general"), QStringLiteral("more")}, {QStringLiteral("appearance"), QStringLiteral("blur")},
        {QStringLiteral("capture"), QStringLiteral("modeStatic")}, {QStringLiteral("pin"), QStringLiteral("pin")},
        {QStringLiteral("annotation"), QStringLiteral("pencil")}, {QStringLiteral("output"), QStringLiteral("save")},
        {QStringLiteral("control"), QStringLiteral("arrow")}, {QStringLiteral("recognition"), QStringLiteral("codes")},
        {QStringLiteral("record"), QStringLiteral("modeRecord")}, {QStringLiteral("advanced"), QStringLiteral("more")},
    };
    return toolbarIcon(icons.value(page), color);
}

// 颜色色块：带透明度的颜色画在棋盘格上；无效颜色（空值）画一条斜线
QIcon swatchIcon(const QColor& color, const QColor& border)
{
    QPixmap pm(36, 36);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath shape;
    shape.addRoundedRect(QRectF(1, 1, 34, 34), 7, 7);
    p.setClipPath(shape);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            p.fillRect(x * 9, y * 9, 9, 9, (x + y) % 2 ? QColor(0xcc, 0xcc, 0xcc) : Qt::white);
    if (color.isValid()) {
        p.fillRect(pm.rect(), color);
    } else {
        p.setPen(QPen(QColor(0xe0, 0x40, 0x40), 2));
        p.drawLine(6, 30, 30, 6);
    }
    p.setClipping(false);
    p.setPen(QPen(border, 1.5));
    p.setBrush(Qt::NoBrush);
    p.drawPath(shape);
    pm.setDevicePixelRatio(2.0);
    return QIcon(pm);
}
} // namespace

PreferencesDialog::PreferencesDialog(Settings& settings, QWidget* parent)
    : QDialog(parent, Qt::Window | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint)
    , m_settings(settings)
{
    setWindowTitle(tr("Preferences"));
    setMinimumSize(760, 520);
    resize(960, 680);
    setStyleSheet(prefs::styleSheet());
    buildPages();
    theme::decorateWindow(this);
}

QString PreferencesDialog::pageTitle(const QString& page)
{
    static const QHash<QString, const char*> titles{
        {QStringLiteral("general"), QT_TR_NOOP("General")}, {QStringLiteral("appearance"), QT_TR_NOOP("Appearance")},
        {QStringLiteral("capture"), QT_TR_NOOP("Capture")}, {QStringLiteral("pin"), QT_TR_NOOP("Pin")},
        {QStringLiteral("annotation"), QT_TR_NOOP("Annotation")}, {QStringLiteral("output"), QT_TR_NOOP("Output")},
        {QStringLiteral("control"), QT_TR_NOOP("Control")}, {QStringLiteral("recognition"), QT_TR_NOOP("Recognition")},
        {QStringLiteral("record"), QT_TR_NOOP("Recording")}, {QStringLiteral("advanced"), QT_TR_NOOP("Advanced")},
    };
    const char* t = titles.value(page, nullptr);
    return t ? tr(t) : page;
}

void PreferencesDialog::buildPages()
{
    // 布局与 AITranslator 主窗口一致：标题栏（decorateWindow）| 左侧导航 + 右侧内容面板 | 底部状态栏
    const theme::Colors& colors = theme::colors();
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    auto* body = new QHBoxLayout;
    body->setSpacing(0);
    root->addLayout(body, 1);

    m_nav = new QListWidget(this);
    m_nav->setObjectName(QStringLiteral("nav"));
    m_nav->setFixedWidth(176);
    m_nav->setItemDelegate(new prefs::NavDelegate(m_nav));
    m_nav->setMouseTracking(true);
    m_nav->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    body->addWidget(m_nav);

    // 内容面板：顶部栏（右侧搜索框，下方金色分隔线），下面每页一个滚动区域，页标题 + 卡片
    constexpr int kHeaderHeight = 49;
    auto* content = new prefs::ContentPanel(this);
    content->setHeaderHeight(kHeaderHeight);
    auto* panel = new QVBoxLayout(content);
    panel->setContentsMargins(1, 1, 0, 0);
    panel->setSpacing(0);
    auto* header = new QHBoxLayout;
    header->setContentsMargins(28, 0, 28, 1);
    header->addStretch(1);
    auto* search = new QLineEdit(content);
    search->setObjectName(QStringLiteral("search"));
    search->setPlaceholderText(tr("Search"));
    search->setClearButtonEnabled(true);
    search->setFixedWidth(240);
    search->addAction(toolbarIcon(QStringLiteral("magnifier"), colors.controlTextSecondary), QLineEdit::LeadingPosition);
    header->addWidget(search);
    auto* headerBox = new QWidget(content);
    headerBox->setFixedHeight(kHeaderHeight - 1);
    headerBox->setLayout(header);
    panel->addWidget(headerBox);
    m_stack = new QStackedWidget(content);
    panel->addWidget(m_stack, 1);
    body->addWidget(content, 1);

    auto* status = new prefs::StatusBar(this);
    status->setFixedHeight(34);
    auto* statusRow = new QHBoxLayout(status);
    statusRow->setContentsMargins(12, 1, 12, 0);
    auto* statusText = new QLabel(tr("Changes take effect immediately"), status);
    statusText->setObjectName(QStringLiteral("status"));
    statusRow->addWidget(statusText);
    root->addWidget(status);

    // 按首次出现顺序分页
    QStringList pages;
    for (const auto& spec : settingSpecs())
        if (!pages.contains(spec.page))
            pages.append(spec.page);
    pages.removeAll(QStringLiteral("hidden")); // CFG-06：高级配置只在配置文件里
    for (int i = 0; i < pages.size(); ++i) {
        auto* scroll = new QScrollArea(m_stack);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto* page = new QWidget(scroll);
        page->setObjectName(QStringLiteral("page"));
        auto* v = new QVBoxLayout(page);
        v->setContentsMargins(28, 20, 28, 24);
        v->setSpacing(18);
        auto* title = new QLabel(pageTitle(pages[i]), page);
        title->setObjectName(QStringLiteral("pageTitle"));
        auto* card = new prefs::Card(page);
        auto* rows = new QVBoxLayout(card);
        rows->setContentsMargins(0, 0, 0, 0);
        rows->setSpacing(0);
        for (const auto& spec : settingSpecs()) {
            if (spec.page != pages[i])
                continue;
            QWidget* row = makeRow(spec, card);
            rows->addWidget(row);
            m_rows.append({row, tr2(spec.title) + QLatin1Char('\n') + tr2(spec.description), i});
        }
        v->addWidget(title);
        v->addWidget(card);
        v->addStretch(1);
        scroll->setWidget(page);
        m_stack->addWidget(scroll);
        m_nav->addItem(new QListWidgetItem(pageIcon(pages[i], colors.controlText), pageTitle(pages[i])));
    }
    m_empty = new QLabel(tr("No matching settings"), m_stack);
    m_empty->setObjectName(QStringLiteral("empty"));
    m_empty->setAlignment(Qt::AlignCenter);
    m_stack->addWidget(m_empty);

    connect(m_nav, &QListWidget::currentRowChanged, m_stack, &QStackedWidget::setCurrentIndex);
    connect(search, &QLineEdit::textChanged, this, &PreferencesDialog::filter);
    m_nav->setCurrentRow(0);
    m_nav->setFocus();
}

QWidget* PreferencesDialog::makeRow(const SettingSpec& spec, QWidget* parent)
{
    auto* row = new QWidget(parent);
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(18, 12, 18, 12);
    h->setSpacing(24);
    auto* texts = new QVBoxLayout;
    texts->setSpacing(2);
    // 标题与说明都可换行，最小宽度不受长串（如文件名变量列表）影响，窄窗口里不会把编辑器挤出卡片
    auto* label = new QLabel(tr2(spec.title), row);
    label->setObjectName(QStringLiteral("rowTitle"));
    label->setToolTip(tr2(spec.description));
    label->setWordWrap(true);
    label->setMinimumWidth(1);
    auto* desc = new QLabel(tr2(spec.description), row);
    desc->setObjectName(QStringLiteral("rowDesc"));
    desc->setWordWrap(true);
    desc->setMinimumWidth(1);
    texts->addWidget(label);
    texts->addWidget(desc);

    QWidget* editor = makeEditor(spec);
    editor->setParent(row);
    editor->setToolTip(tr2(spec.description));
    editor->setAccessibleName(tr2(spec.title));            // NFR-14：屏幕阅读器读出选项名与说明
    editor->setAccessibleDescription(tr2(spec.description));
    label->setBuddy(editor);
    editor->setObjectName(spec.key); // 测试按键名查找
    h->addLayout(texts, 1);
    h->addWidget(editor, 0, Qt::AlignRight | Qt::AlignVCenter);
    return row;
}

void PreferencesDialog::filter(const QString& text)
{
    // 按标题与说明匹配；没有匹配项的分类从侧边栏隐藏
    const QString needle = text.trimmed();
    QList<bool> pageMatched(m_nav->count(), false);
    for (const Row& row : m_rows) {
        const bool match = needle.isEmpty() || row.text.contains(needle, Qt::CaseInsensitive);
        row.widget->setVisible(match);
        pageMatched[row.page] = pageMatched[row.page] || match;
    }
    int current = m_nav->currentRow();
    for (int i = 0; i < m_nav->count(); ++i)
        m_nav->item(i)->setHidden(!pageMatched[i]);
    if (current < 0 || !pageMatched[current])
        current = static_cast<int>(pageMatched.indexOf(true));
    m_nav->setCurrentRow(current);
    if (current < 0)
        m_stack->setCurrentWidget(m_empty);
    else
        m_stack->setCurrentIndex(current);
}

QWidget* PreferencesDialog::makeEditor(const SettingSpec& spec)
{
    const QString key = spec.key;
    Settings& s = m_settings;
    switch (spec.type) {
    case SettingType::Bool: {
        auto* box = new prefs::SwitchButton(this);
        box->setChecked(s.boolValue(key));
        connect(box, &QCheckBox::toggled, this, [&s, key](bool v) { s.setValue(key, v); });
        return box;
    }
    case SettingType::Int: {
        auto* spin = new prefs::FocusWheel<QSpinBox>(this);
        spin->setRange(spec.minimum.isValid() ? spec.minimum.toInt() : -1000000, spec.maximum.isValid() ? spec.maximum.toInt() : 1000000);
        spin->setValue(s.intValue(key));
        spin->setFocusPolicy(Qt::StrongFocus);
        spin->setFixedWidth(120);
        connect(spin, &QSpinBox::valueChanged, this, [&s, key](int v) { s.setValue(key, v); });
        return spin;
    }
    case SettingType::Double: {
        auto* spin = new prefs::FocusWheel<QDoubleSpinBox>(this);
        spin->setRange(spec.minimum.isValid() ? spec.minimum.toDouble() : -1e6, spec.maximum.isValid() ? spec.maximum.toDouble() : 1e6);
        spin->setValue(s.value(key).toDouble());
        spin->setFocusPolicy(Qt::StrongFocus);
        spin->setFixedWidth(120);
        connect(spin, &QDoubleSpinBox::valueChanged, this, [&s, key](double v) { s.setValue(key, v); });
        return spin;
    }
    case SettingType::Enum: {
        auto* combo = new prefs::FocusWheel<QComboBox>(this);
        combo->setFocusPolicy(Qt::StrongFocus);
        for (int i = 0; i < spec.choices.size(); ++i)
            combo->addItem(tr2(spec.choiceTitles.value(i, spec.choices[i].toUtf8().constData())), spec.choices[i]);
        combo->setCurrentIndex(std::max(0, static_cast<int>(spec.choices.indexOf(s.stringValue(key)))));
        combo->setMinimumWidth(180);
        combo->setMaximumWidth(260);
        connect(combo, &QComboBox::currentIndexChanged, this, [&s, key, combo](int i) { s.setValue(key, combo->itemData(i).toString()); });
        return combo;
    }
    case SettingType::KeySequence: {
        auto* edit = new QKeySequenceEdit(QKeySequence(s.stringValue(key)), this);
        edit->setMaximumSequenceLength(1);
        edit->setClearButtonEnabled(true);
        edit->setFixedWidth(180);
        connect(edit, &QKeySequenceEdit::editingFinished, this, [&s, key, edit] { s.setValue(key, edit->keySequence().toString(QKeySequence::PortableText)); });
        return edit;
    }
    case SettingType::Directory: {
        auto* row = new QWidget(this);
        row->setFixedWidth(320);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(8);
        auto* edit = new QLineEdit(s.stringValue(key), row);
        edit->setObjectName(key + QStringLiteral(".edit"));
        auto* browse = new QPushButton(tr("Browse..."), row);
        browse->setAutoDefault(false);
        h->addWidget(edit, 1);
        h->addWidget(browse);
        connect(edit, &QLineEdit::editingFinished, this, [&s, key, edit] { s.setValue(key, edit->text()); });
        connect(browse, &QPushButton::clicked, this, [this, &s, key, edit] {
            const QString dir = QFileDialog::getExistingDirectory(this, tr("Choose folder"), edit->text());
            if (!dir.isEmpty()) {
                edit->setText(dir);
                s.setValue(key, dir);
            }
        });
        return row;
    }
    case SettingType::Color: {
        // 色块打开取色器（含透明度）；文本框可直接输入，留空表示用默认
        auto* row = new QWidget(this);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(8);
        auto* swatch = new QPushButton(row);
        swatch->setObjectName(key + QStringLiteral(".swatch"));
        swatch->setIconSize(QSize(18, 18));
        swatch->setFixedSize(32, 32);
        swatch->setAutoDefault(false);
        swatch->setCursor(Qt::PointingHandCursor);
        swatch->setStyleSheet(QStringLiteral("padding: 0;"));
        auto* edit = new QLineEdit(s.stringValue(key), row);
        edit->setFixedWidth(120);
        const QColor border = theme::colors().border;
        auto apply = [&s, key, edit, swatch, border](const QString& text) {
            s.setValue(key, text);
            swatch->setIcon(swatchIcon(QColor::fromString(text), border));
        };
        swatch->setIcon(swatchIcon(QColor::fromString(edit->text()), border));
        h->addWidget(swatch);
        h->addWidget(edit);
        connect(edit, &QLineEdit::editingFinished, this, [apply, edit] { apply(edit->text()); });
        connect(swatch, &QPushButton::clicked, this, [this, apply, edit] {
            const QColor c = theme::getColor(QColor::fromString(edit->text()), this, QString(), QColorDialog::ShowAlphaChannel);
            if (c.isValid()) {
                edit->setText(c.name(QColor::HexArgb));
                apply(edit->text());
            }
        });
        return row;
    }
    case SettingType::String: {
        auto* edit = new QLineEdit(s.stringValue(key), this);
        edit->setFixedWidth(240);
        connect(edit, &QLineEdit::editingFinished, this, [&s, key, edit] { s.setValue(key, edit->text()); });
        return edit;
    }
    }
    return new QWidget(this);
}

} // namespace ss
