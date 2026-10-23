#pragma once

#include <QImage>
#include <QList>

namespace ss {

// 多级缩小图（NFR-04、R-07）：为大图预先生成逐级减半的副本，缩放显示时取最接近的一级，
// 避免每帧对原图做大比例缩放。
class Mipmap {
public:
    Mipmap() = default;
    explicit Mipmap(const QImage& original, int minSize = 64);

    bool isEmpty() const { return m_levels.isEmpty(); }
    const QImage& original() const { return m_levels.isEmpty() ? m_empty : m_levels.first(); }
    int levelCount() const { return m_levels.size(); }

    // 显示比例 scale（1.0 为原尺寸）对应的最合适一级：选像素数不小于目标尺寸的最小一级。
    const QImage& levelForScale(qreal scale) const;
    int levelIndexForScale(qreal scale) const;

private:
    QList<QImage> m_levels;
    QImage m_empty;
};

} // namespace ss
