#include "core/code/CodeDecoder.h"

#include <QUrl>
#include <BarcodeFormat.h>
#include <BitMatrix.h>
#include <MultiFormatWriter.h>
#include <ReadBarcode.h>

namespace ss {

bool CodeResult::isUrl() const
{
    const QUrl url(text.trimmed());
    return url.isValid() && (url.scheme() == QLatin1String("http") || url.scheme() == QLatin1String("https"));
}

QList<CodeResult> decodeCodes(const QImage& image)
{
    QList<CodeResult> out;
    if (image.isNull())
        return out;
    const QImage gray = image.convertToFormat(QImage::Format_Grayscale8);
    const ZXing::ImageView view(gray.constBits(), gray.width(), gray.height(), ZXing::ImageFormat::Lum, static_cast<int>(gray.bytesPerLine()));
    ZXing::ReaderOptions options;
    options.setTryHarder(true);
    options.setTryRotate(true);
    options.setTryInvert(true);
    options.setFormats(ZXing::BarcodeFormat::Any);
    for (const ZXing::Barcode& b : ZXing::ReadBarcodes(view, options)) {
        if (!b.isValid())
            continue;
        CodeResult r;
        r.text = QString::fromStdString(b.text());
        r.format = QString::fromStdString(ZXing::ToString(b.format()));
        const auto& pos = b.position();
        int l = pos[0].x, t = pos[0].y, rr = l, bb = t;
        for (const auto& p : pos) {
            l = std::min(l, p.x);
            t = std::min(t, p.y);
            rr = std::max(rr, p.x);
            bb = std::max(bb, p.y);
        }
        r.bounds = QRect(QPoint(l, t), QPoint(rr, bb));
        out.append(r);
    }
    return out;
}

QImage encodeQrCode(const QString& text, int module, int quietZone)
{
    ZXing::MultiFormatWriter writer(ZXing::BarcodeFormat::QRCode);
    writer.setMargin(quietZone);
    const ZXing::BitMatrix matrix = writer.encode(text.toStdString(), 0, 0);
    QImage img(matrix.width() * module, matrix.height() * module, QImage::Format_RGB32);
    img.fill(Qt::white);
    for (int y = 0; y < matrix.height(); ++y)
        for (int x = 0; x < matrix.width(); ++x)
            if (matrix.get(x, y))
                for (int dy = 0; dy < module; ++dy)
                    for (int dx = 0; dx < module; ++dx)
                        img.setPixel(x * module + dx, y * module + dy, qRgb(0, 0, 0));
    return img;
}

} // namespace ss
