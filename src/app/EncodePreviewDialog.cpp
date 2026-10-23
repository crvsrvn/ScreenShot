#include "app/EncodePreviewDialog.h"

#include "app/OutputActions.h"
#include "core/pin/FilePin.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QSlider>
#include <QVBoxLayout>

namespace ss {

EncodePreviewDialog::EncodePreviewDialog(const QImage& image, OutputActions& output, QWidget* parent)
    : QDialog(parent)
    , m_image(image)
    , m_output(output)
    , m_format(new QComboBox(this))
    , m_quality(new QSlider(Qt::Horizontal, this))
    , m_size(new QLabel(this))
    , m_view(new QLabel(this))
{
    setWindowTitle(tr("Encoding preview"));
    resize(640, 480);
    auto* layout = new QVBoxLayout(this);
    auto* row = new QHBoxLayout;
    m_format->setObjectName(QStringLiteral("format"));
    m_format->addItems({QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("webp")});
    row->addWidget(new QLabel(tr("Format"), this));
    row->addWidget(m_format);
    row->addWidget(new QLabel(tr("Quality"), this));
    m_quality->setObjectName(QStringLiteral("quality"));
    m_quality->setRange(1, 100);
    m_quality->setValue(85);
    row->addWidget(m_quality, 1);
    m_size->setObjectName(QStringLiteral("size"));
    row->addWidget(m_size);
    layout->addLayout(row);
    m_view->setAlignment(Qt::AlignCenter);
    m_view->setMinimumHeight(240);
    layout->addWidget(m_view, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    connect(m_format, &QComboBox::currentTextChanged, this, [this] { refresh(); });
    connect(m_quality, &QSlider::valueChanged, this, [this] { refresh(); });
    refresh();
}

QString EncodePreviewDialog::format() const
{
    return m_format->currentText();
}

int EncodePreviewDialog::quality() const
{
    return m_quality->value();
}

void EncodePreviewDialog::setFormat(const QString& format)
{
    m_format->setCurrentText(format);
}

void EncodePreviewDialog::setQuality(int quality)
{
    m_quality->setValue(quality);
}

void EncodePreviewDialog::refresh()
{
    const QString f = format();
    m_quality->setEnabled(f != QLatin1String("png"));
    const QByteArray encoded = m_output.encode(m_image, f, f == QLatin1String("png") ? -1 : quality());
    m_bytes = encoded.size();
    m_size->setText(humanFileSize(m_bytes));
    m_preview = QImage::fromData(encoded, f.toLatin1().constData());
    if (!m_preview.isNull())
        m_view->setPixmap(QPixmap::fromImage(m_preview.scaled(m_view->size().expandedTo(QSize(320, 240)), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
}

} // namespace ss
