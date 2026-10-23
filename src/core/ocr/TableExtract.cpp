#include "core/ocr/TableExtract.h"

#include <algorithm>

namespace ss {

QList<QStringList> extractTable(const QList<OcrLine>& linesIn)
{
    QList<OcrLine> lines = linesIn;
    lines.erase(std::remove_if(lines.begin(), lines.end(), [](const OcrLine& l) { return l.text.trimmed().isEmpty() || l.bounds.isNull(); }), lines.end());
    if (lines.isEmpty())
        return {};
    // 行聚类：按 y 排序，与当前行纵向重叠过半归入同一行
    std::sort(lines.begin(), lines.end(), [](const OcrLine& a, const OcrLine& b) { return a.bounds.top() < b.bounds.top(); });
    QList<QList<OcrLine>> rows;
    for (const OcrLine& l : lines) {
        if (!rows.isEmpty()) {
            const QRect& ref = rows.last().first().bounds;
            const int overlap = std::min(ref.bottom(), l.bounds.bottom()) - std::max(ref.top(), l.bounds.top());
            if (overlap > std::min(ref.height(), l.bounds.height()) / 2) {
                rows.last().append(l);
                continue;
            }
        }
        rows.append({l});
    }
    // 列边界：收集所有单元格左边界，相距小于一个字高的合并
    int charHeight = 0;
    for (const OcrLine& l : lines)
        charHeight = std::max(charHeight, l.bounds.height());
    QList<int> lefts;
    for (const OcrLine& l : lines)
        lefts.append(l.bounds.left());
    std::sort(lefts.begin(), lefts.end());
    QList<int> columns;
    for (int x : lefts)
        if (columns.isEmpty() || x - columns.last() > charHeight)
            columns.append(x);
    auto columnOf = [&](int left) {
        int best = 0;
        for (int i = 0; i < columns.size(); ++i)
            if (std::abs(left - columns[i]) < std::abs(left - columns[best]))
                best = i;
        return best;
    };
    QList<QStringList> table;
    for (QList<OcrLine>& row : rows) {
        std::sort(row.begin(), row.end(), [](const OcrLine& a, const OcrLine& b) { return a.bounds.left() < b.bounds.left(); });
        QStringList cells(columns.size());
        for (const OcrLine& l : row) {
            const int c = columnOf(l.bounds.left());
            cells[c] = cells[c].isEmpty() ? l.text.trimmed() : cells[c] + QLatin1Char(' ') + l.text.trimmed();
        }
        table.append(cells);
    }
    return table;
}

} // namespace ss
