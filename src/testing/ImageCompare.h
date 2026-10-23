#pragma once

#include <QImage>
#include <QPoint>
#include <QString>

namespace ss::testing {

// 图像比对（TST-04）。用例必须显式给出允许的差异。
struct CompareOptions {
    int channelTolerance = 0;      // 每通道允许的最大差值
    qint64 maxDifferingPixels = 0; // 超出容差的像素数上限
};

struct CompareResult {
    bool sizeMismatch = false;
    qint64 differingPixels = 0;    // 超出容差的像素数
    int maxChannelDiff = 0;        // 全图最大通道差
    QPoint firstDiff{-1, -1};
    bool passed = false;
    QString summary() const;
};

CompareResult compareImages(const QImage& actual, const QImage& expected, const CompareOptions& options = {});

// 生成差异图：相同处灰度减淡，不同处红色。
QImage diffImage(const QImage& actual, const QImage& expected);

// 比对失败时把实际图、期望图、差异图写到目录，返回是否写入成功。
bool saveFailureArtifacts(const QString& dir, const QString& name, const QImage& actual, const QImage& expected);

} // namespace ss::testing
