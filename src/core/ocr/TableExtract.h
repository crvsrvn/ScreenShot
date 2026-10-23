#pragma once

#include "core/ports/IOcrEngine.h"

#include <QList>
#include <QStringList>

namespace ss {

// 表格识别（REC-08）：按识别行的位置聚成行与列。
// 行：纵向重叠的行归为一行；列：所有单元格的横向区间聚成若干列（相距超过一个字高视为新列）。
QList<QStringList> extractTable(const QList<OcrLine>& lines);

} // namespace ss
