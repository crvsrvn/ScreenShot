#include "platform/macos/MacOcr.h"

#import <Vision/Vision.h>
#include <CoreGraphics/CoreGraphics.h>

namespace ss::mac {

namespace {

CGImageRef qimageToCGImage(const QImage& source)
{
    const QImage img = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(const_cast<uchar*>(img.constBits()), static_cast<size_t>(img.width()), static_cast<size_t>(img.height()), 8,
                                             static_cast<size_t>(img.bytesPerLine()), colorSpace, kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
    CGImageRef cg = CGBitmapContextCreateImage(ctx);
    CGContextRelease(ctx);
    CGColorSpaceRelease(colorSpace);
    return cg;
}

} // namespace

OcrResult MacOcr::recognize(const QImage& image)
{
    OcrResult result;
    if (image.isNull()) {
        result.error = QStringLiteral("空图像");
        return result;
    }
    CGImageRef cg = qimageToCGImage(image);
    VNRecognizeTextRequest* request = [[VNRecognizeTextRequest alloc] init];
    request.recognitionLevel = VNRequestTextRecognitionLevelAccurate;
    request.usesLanguageCorrection = YES;
    request.recognitionLanguages = @[ @"zh-Hans", @"en-US" ];
    VNImageRequestHandler* handler = [[VNImageRequestHandler alloc] initWithCGImage:cg options:@{}];
    NSError* error = nil;
    const bool ok = [handler performRequests:@[ request ] error:&error];
    if (!ok) {
        result.error = QString::fromNSString(error.localizedDescription ?: @"识别失败");
        CGImageRelease(cg);
        return result;
    }
    const CGFloat w = image.width();
    const CGFloat h = image.height();
    for (VNRecognizedTextObservation* obs in request.results) {
        VNRecognizedText* best = [obs topCandidates:1].firstObject;
        if (!best)
            continue;
        OcrLine line;
        line.text = QString::fromNSString(best.string);
        // Vision 的框是左下角原点的归一化坐标
        const CGRect b = obs.boundingBox;
        line.bounds = QRect(static_cast<int>(b.origin.x * w), static_cast<int>((1.0 - b.origin.y - b.size.height) * h),
                            static_cast<int>(b.size.width * w), static_cast<int>(b.size.height * h));
        result.lines.append(line);
    }
    // Vision 按置信度排序，这里改按从上到下
    std::sort(result.lines.begin(), result.lines.end(), [](const OcrLine& a, const OcrLine& b) { return a.bounds.top() < b.bounds.top(); });
    result.ok = true;
    CGImageRelease(cg);
    return result;
}

} // namespace ss::mac
