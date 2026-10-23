#include "core/util/Mipmap.h"

#include <algorithm>

namespace ss {

Mipmap::Mipmap(const QImage& original, int minSize)
{
    if (original.isNull())
        return;
    m_levels.append(original);
    QImage current = original;
    while (current.width() / 2 >= minSize && current.height() / 2 >= minSize) {
        current = current.scaled(current.size() / 2, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        m_levels.append(current);
    }
}

int Mipmap::levelIndexForScale(qreal scale) const
{
    if (m_levels.isEmpty())
        return 0;
    // 第 i 级的比例是 2^-i；取第一个比例小于等于 scale 的级别的上一级
    int index = 0;
    qreal levelScale = 1.0;
    while (index + 1 < m_levels.size() && levelScale / 2 >= scale) {
        levelScale /= 2;
        ++index;
    }
    return index;
}

const QImage& Mipmap::levelForScale(qreal scale) const
{
    if (m_levels.isEmpty())
        return m_empty;
    return m_levels.at(levelIndexForScale(scale));
}

} // namespace ss
