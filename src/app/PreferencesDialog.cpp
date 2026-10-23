#include "app/PreferencesDialog.h"

#include "core/config/Settings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

namespace ss {

namespace {
QString tr2(const char* text)
{
    return QCoreApplication::translate("Settings", text);
}
} // namespace

PreferencesDialog::PreferencesDialog(Settings& settings, QWidget* parent)
    : QDialog(parent)
    , m_settings(settings)
{
    setWindowTitle(tr("Preferences"));
    setMinimumSize(560, 420);
    buildPages();
}

QString PreferencesDialog::pageTitle(const QString& page)
{
    static const QHash<QString, const char*> titles{
        {QStringLiteral("general"), QT_TR_NOOP("General")}, {QStringLiteral("appearance"), QT_TR_NOOP("Appearance")},
        {QStringLiteral("capture"), QT_TR_NOOP("Capture")}, {QStringLiteral("pin"), QT_TR_NOOP("Pin")},
        {QStringLiteral("annotation"), QT_TR_NOOP("Annotation")}, {QStringLiteral("output"), QT_TR_NOOP("Output")},
        {QStringLiteral("control"), QT_TR_NOOP("Control")}, {QStringLiteral("recognition"), QT_TR_NOOP("Recognition")},
        {QStringLiteral("advanced"), QT_TR_NOOP("Advanced")},
    };
    const char* t = titles.value(page, nullptr);
    return t ? tr(t) : page;
}

void PreferencesDialog::buildPages()
{
    auto* layout = new QVBoxLayout(this);
    auto* tabs = new QTabWidget(this);
    layout->addWidget(tabs);
    // 按首次出现顺序分页
    QStringList pages;
    for (const auto& spec : settingSpecs())
        if (!pages.contains(spec.page))
            pages.append(spec.page);
    pages.removeAll(QStringLiteral("hidden")); // CFG-06：高级配置只在配置文件里
    for (const QString& page : pages) {
        auto* scroll = new QScrollArea(tabs);
        scroll->setWidgetResizable(true);
        auto* content = new QWidget(scroll);
        auto* form = new QFormLayout(content);
        for (const auto& spec : settingSpecs()) {
            if (spec.page != page)
                continue;
            auto* label = new QLabel(tr2(spec.title), content);
            label->setToolTip(tr2(spec.description));
            QWidget* editor = makeEditor(spec);
            editor->setToolTip(tr2(spec.description));
            editor->setObjectName(spec.key); // 测试按键名查找
            form->addRow(label, editor);
        }
        scroll->setWidget(content);
        tabs->addTab(scroll, pageTitle(page));
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    layout->addWidget(buttons);
}

QWidget* PreferencesDialog::makeEditor(const SettingSpec& spec)
{
    const QString key = spec.key;
    Settings& s = m_settings;
    switch (spec.type) {
    case SettingType::Bool: {
        auto* box = new QCheckBox(this);
        box->setChecked(s.boolValue(key));
        connect(box, &QCheckBox::toggled, this, [&s, key](bool v) { s.setValue(key, v); });
        return box;
    }
    case SettingType::Int: {
        auto* spin = new QSpinBox(this);
        spin->setRange(spec.minimum.isValid() ? spec.minimum.toInt() : -1000000, spec.maximum.isValid() ? spec.maximum.toInt() : 1000000);
        spin->setValue(s.intValue(key));
        connect(spin, &QSpinBox::valueChanged, this, [&s, key](int v) { s.setValue(key, v); });
        return spin;
    }
    case SettingType::Double: {
        auto* spin = new QDoubleSpinBox(this);
        spin->setRange(spec.minimum.isValid() ? spec.minimum.toDouble() : -1e6, spec.maximum.isValid() ? spec.maximum.toDouble() : 1e6);
        spin->setValue(s.value(key).toDouble());
        connect(spin, &QDoubleSpinBox::valueChanged, this, [&s, key](double v) { s.setValue(key, v); });
        return spin;
    }
    case SettingType::Enum: {
        auto* combo = new QComboBox(this);
        for (int i = 0; i < spec.choices.size(); ++i)
            combo->addItem(tr2(spec.choiceTitles.value(i, spec.choices[i].toUtf8().constData())), spec.choices[i]);
        combo->setCurrentIndex(std::max(0, static_cast<int>(spec.choices.indexOf(s.stringValue(key)))));
        connect(combo, &QComboBox::currentIndexChanged, this, [&s, key, combo](int i) { s.setValue(key, combo->itemData(i).toString()); });
        return combo;
    }
    case SettingType::KeySequence: {
        auto* edit = new QKeySequenceEdit(QKeySequence(s.stringValue(key)), this);
        edit->setMaximumSequenceLength(1);
        connect(edit, &QKeySequenceEdit::editingFinished, this, [&s, key, edit] { s.setValue(key, edit->keySequence().toString(QKeySequence::PortableText)); });
        return edit;
    }
    case SettingType::Directory: {
        auto* row = new QWidget(this);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        auto* edit = new QLineEdit(s.stringValue(key), row);
        edit->setObjectName(key + QStringLiteral(".edit"));
        auto* browse = new QPushButton(tr("Browse..."), row);
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
    case SettingType::String:
    case SettingType::Color: {
        auto* edit = new QLineEdit(s.stringValue(key), this);
        connect(edit, &QLineEdit::editingFinished, this, [&s, key, edit] { s.setValue(key, edit->text()); });
        return edit;
    }
    }
    return new QWidget(this);
}

} // namespace ss
