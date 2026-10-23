#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

namespace ss {

// 最小 .xlsx 写出（REC-08）：一个工作表，全部单元格为内联字符串；zip 容器用“存储”方式，不依赖任何压缩库
QByteArray buildXlsx(const QList<QStringList>& rows, const QString& sheetName = QStringLiteral("Sheet1"));
bool writeXlsx(const QString& path, const QList<QStringList>& rows, const QString& sheetName = QStringLiteral("Sheet1"));

// 存储式 zip（便于测试与其他导出复用）
struct ZipEntry {
    QString name;
    QByteArray data;
};
QByteArray buildStoredZip(const QList<ZipEntry>& entries);

} // namespace ss
