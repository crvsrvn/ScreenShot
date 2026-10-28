#include "app/CustomCaptureDialog.h"

#include "app/WindowChrome.h"
#include "core/config/Settings.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>

namespace ss {

CustomCaptureDialog::CustomCaptureDialog(Settings& settings, QWidget* parent)
    : QDialog(parent)
    , m_settings(settings)
{
    setWindowTitle(tr("Custom capture"));
    auto* form = new QFormLayout(this);
    auto makeSpin = [this](int min, int max, int value) {
        auto* s = new QSpinBox(this);
        s->setRange(min, max);
        s->setValue(value);
        return s;
    };
    m_x = makeSpin(-100000, 100000, 0);
    m_y = makeSpin(-100000, 100000, 0);
    m_w = makeSpin(1, 100000, 800);
    m_h = makeSpin(1, 100000, 600);
    m_delay = makeSpin(0, 60000, 0);
    m_delay->setSuffix(QStringLiteral(" ms"));
    m_x->setObjectName(QStringLiteral("x"));
    m_y->setObjectName(QStringLiteral("y"));
    m_w->setObjectName(QStringLiteral("w"));
    m_h->setObjectName(QStringLiteral("h"));
    m_delay->setObjectName(QStringLiteral("delay"));
    form->addRow(tr("X"), m_x);
    form->addRow(tr("Y"), m_y);
    form->addRow(tr("Width"), m_w);
    form->addRow(tr("Height"), m_h);
    form->addRow(tr("Delay"), m_delay);

    auto* presetRow = new QWidget(this);
    auto* presetLayout = new QHBoxLayout(presetRow);
    presetLayout->setContentsMargins(0, 0, 0, 0);
    m_presets = new QComboBox(presetRow);
    m_presets->setObjectName(QStringLiteral("presets"));
    m_presetName = new QLineEdit(presetRow);
    m_presetName->setPlaceholderText(tr("Preset name"));
    auto* save = new QPushButton(tr("Save preset"), presetRow);
    auto* remove = new QPushButton(tr("Delete"), presetRow);
    presetLayout->addWidget(m_presets, 1);
    presetLayout->addWidget(m_presetName, 1);
    presetLayout->addWidget(save);
    presetLayout->addWidget(remove);
    form->addRow(tr("Presets"), presetRow);
    connect(m_presets, &QComboBox::activated, this, &CustomCaptureDialog::applyPreset);
    connect(save, &QPushButton::clicked, this, [this] { saveCurrentAsPreset(m_presetName->text().trimmed()); });
    connect(remove, &QPushButton::clicked, this, [this] {
        if (m_presets->currentIndex() >= 0) {
            removePreset(m_settings, m_presets->currentText());
            reloadPresets();
        }
    });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Capture"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    form->addRow(buttons);
    reloadPresets();
    theme::decorateWindow(this);
}

CustomCaptureDialog::Request CustomCaptureDialog::request() const
{
    return {QRect(m_x->value(), m_y->value(), m_w->value(), m_h->value()), m_delay->value()};
}

void CustomCaptureDialog::setRegion(const QRect& r)
{
    m_x->setValue(r.x());
    m_y->setValue(r.y());
    m_w->setValue(std::max(1, r.width()));
    m_h->setValue(std::max(1, r.height()));
}

void CustomCaptureDialog::setDelayMs(int ms)
{
    m_delay->setValue(ms);
}

QJsonArray CustomCaptureDialog::presets(const Settings& settings)
{
    return QJsonDocument::fromJson(settings.stringValue(QStringLiteral("capture.presets")).toUtf8()).array();
}

void CustomCaptureDialog::savePreset(Settings& settings, const QString& name, const QRect& region, int delayMs)
{
    QJsonArray arr = presets(settings);
    QJsonObject entry{{QStringLiteral("name"), name}, {QStringLiteral("x"), region.x()}, {QStringLiteral("y"), region.y()}, {QStringLiteral("w"), region.width()},
                      {QStringLiteral("h"), region.height()}, {QStringLiteral("delay"), delayMs}};
    bool replaced = false;
    for (int i = 0; i < arr.size(); ++i) {
        if (arr[i].toObject().value(QStringLiteral("name")).toString() == name) {
            arr[i] = entry;
            replaced = true;
        }
    }
    if (!replaced)
        arr.append(entry);
    settings.setValue(QStringLiteral("capture.presets"), QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

bool CustomCaptureDialog::removePreset(Settings& settings, const QString& name)
{
    QJsonArray arr = presets(settings);
    for (int i = 0; i < arr.size(); ++i) {
        if (arr[i].toObject().value(QStringLiteral("name")).toString() == name) {
            arr.removeAt(i);
            settings.setValue(QStringLiteral("capture.presets"), QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
            return true;
        }
    }
    return false;
}

void CustomCaptureDialog::reloadPresets()
{
    m_presets->clear();
    for (const auto& v : presets(m_settings))
        m_presets->addItem(v.toObject().value(QStringLiteral("name")).toString());
}

void CustomCaptureDialog::applyPreset(int index)
{
    const QJsonArray arr = presets(m_settings);
    if (index < 0 || index >= arr.size())
        return;
    const QJsonObject o = arr[index].toObject();
    setRegion(QRect(o.value(QStringLiteral("x")).toInt(), o.value(QStringLiteral("y")).toInt(), o.value(QStringLiteral("w")).toInt(), o.value(QStringLiteral("h")).toInt()));
    setDelayMs(o.value(QStringLiteral("delay")).toInt());
}

void CustomCaptureDialog::saveCurrentAsPreset(const QString& name)
{
    if (name.isEmpty())
        return;
    const Request r = request();
    savePreset(m_settings, name, r.region, r.delayMs);
    reloadPresets();
    m_presets->setCurrentText(name);
}

} // namespace ss
