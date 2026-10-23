#include "core/output/XlsxWriter.h"

#include <QDataStream>
#include <QFile>
#include <QIODevice>

namespace ss {

namespace {

quint32 crc32(const QByteArray& data)
{
    static quint32 table[256];
    static bool init = false;
    if (!init) {
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    quint32 crc = 0xFFFFFFFFu;
    for (const char ch : data)
        crc = table[(crc ^ static_cast<quint8>(ch)) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

QString columnName(int index)
{
    QString s;
    for (int v = index + 1; v > 0; v = (v - 1) / 26)
        s.prepend(QChar(QLatin1Char('A').toLatin1() + (v - 1) % 26));
    return s;
}

} // namespace

QByteArray buildStoredZip(const QList<ZipEntry>& entries)
{
    QByteArray out;
    QDataStream s(&out, QIODevice::WriteOnly);
    s.setByteOrder(QDataStream::LittleEndian);
    struct Central {
        QByteArray name;
        quint32 crc, size, offset;
    };
    QList<Central> centrals;
    for (const ZipEntry& e : entries) {
        const QByteArray name = e.name.toUtf8();
        const quint32 crc = crc32(e.data);
        const quint32 offset = static_cast<quint32>(out.size());
        s << quint32(0x04034b50) << quint16(20) << quint16(0x0800) << quint16(0) << quint16(0) << quint16(0) << crc << quint32(e.data.size()) << quint32(e.data.size())
          << quint16(name.size()) << quint16(0);
        s.writeRawData(name.constData(), name.size());
        s.writeRawData(e.data.constData(), e.data.size());
        centrals.append({name, crc, static_cast<quint32>(e.data.size()), offset});
    }
    const quint32 centralStart = static_cast<quint32>(out.size());
    for (const Central& c : centrals) {
        s << quint32(0x02014b50) << quint16(20) << quint16(20) << quint16(0x0800) << quint16(0) << quint16(0) << quint16(0) << c.crc << c.size << c.size << quint16(c.name.size())
          << quint16(0) << quint16(0) << quint16(0) << quint16(0) << quint32(0) << c.offset;
        s.writeRawData(c.name.constData(), c.name.size());
    }
    const quint32 centralSize = static_cast<quint32>(out.size()) - centralStart;
    s << quint32(0x06054b50) << quint16(0) << quint16(0) << quint16(centrals.size()) << quint16(centrals.size()) << centralSize << centralStart << quint16(0);
    return out;
}

QByteArray buildXlsx(const QList<QStringList>& rows, const QString& sheetName)
{
    QString sheet = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                                   "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\"><sheetData>");
    for (int r = 0; r < rows.size(); ++r) {
        sheet += QStringLiteral("<row r=\"%1\">").arg(r + 1);
        for (int c = 0; c < rows[r].size(); ++c) {
            if (rows[r][c].isEmpty())
                continue;
            sheet += QStringLiteral("<c r=\"%1%2\" t=\"inlineStr\"><is><t xml:space=\"preserve\">%3</t></is></c>").arg(columnName(c)).arg(r + 1).arg(rows[r][c].toHtmlEscaped());
        }
        sheet += QStringLiteral("</row>");
    }
    sheet += QStringLiteral("</sheetData></worksheet>");
    const QString workbook = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                                            "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
                                            "<sheets><sheet name=\"%1\" sheetId=\"1\" r:id=\"rId1\"/></sheets></workbook>").arg(sheetName.toHtmlEscaped());
    const QString workbookRels = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                                                "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                                                "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet1.xml\"/></Relationships>");
    const QString rootRels = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                                            "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                                            "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/></Relationships>");
    const QString contentTypes = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                                                "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
                                                "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
                                                "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
                                                "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
                                                "<Override PartName=\"/xl/worksheets/sheet1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/></Types>");
    return buildStoredZip({{QStringLiteral("[Content_Types].xml"), contentTypes.toUtf8()}, {QStringLiteral("_rels/.rels"), rootRels.toUtf8()},
                           {QStringLiteral("xl/workbook.xml"), workbook.toUtf8()}, {QStringLiteral("xl/_rels/workbook.xml.rels"), workbookRels.toUtf8()},
                           {QStringLiteral("xl/worksheets/sheet1.xml"), sheet.toUtf8()}});
}

bool writeXlsx(const QString& path, const QList<QStringList>& rows, const QString& sheetName)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return f.write(buildXlsx(rows, sheetName)) > 0;
}

} // namespace ss
