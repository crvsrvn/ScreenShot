#pragma once

#include <QDialog>
#include <QImage>

class QComboBox;
class QLabel;
class QSlider;

namespace ss {

class OutputActions;

// 保存前预估文件大小、预览编码效果（OUT-07）
class EncodePreviewDialog : public QDialog {
    Q_OBJECT
public:
    EncodePreviewDialog(const QImage& image, OutputActions& output, QWidget* parent = nullptr);

    QString format() const;
    int quality() const;
    qint64 estimatedBytes() const { return m_bytes; }
    QImage preview() const { return m_preview; }
    void setFormat(const QString& format);
    void setQuality(int quality);

private:
    void refresh();
    QImage m_image;
    OutputActions& m_output;
    QComboBox* m_format;
    QSlider* m_quality;
    QLabel* m_size;
    QLabel* m_view;
    qint64 m_bytes = 0;
    QImage m_preview;
};

} // namespace ss
