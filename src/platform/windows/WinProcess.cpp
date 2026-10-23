#include "platform/windows/WinProcess.h"

#include <QFile>

// 只用 winsock2.h 的常量和类型，不调用任何函数，避免导入 ws2_32.dll（零网络）
#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <psapi.h>
#include <vector>

namespace ss::win {

namespace {
// 端口字段是网络字节序，手动换字节序（不用 ntohs）
int portOf(DWORD raw)
{
    return static_cast<int>(((raw & 0xFF) << 8) | ((raw >> 8) & 0xFF));
}
} // namespace

quint64 privateBytes(quint32 pid)
{
    HANDLE h = pid == 0 ? GetCurrentProcess() : OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h)
        return 0;
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    quint64 result = 0;
    if (GetProcessMemoryInfo(h, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)))
        result = pmc.PrivateUsage;
    if (pid != 0)
        CloseHandle(h);
    return result;
}

QStringList networkEndpoints(quint32 pid)
{
    QStringList list;
    ULONG size = 0;
    GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
    std::vector<char> buf(size);
    if (GetExtendedTcpTable(buf.data(), &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0) == NO_ERROR) {
        auto* table = reinterpret_cast<MIB_TCPTABLE_OWNER_PID*>(buf.data());
        for (DWORD i = 0; i < table->dwNumEntries; ++i) {
            const auto& row = table->table[i];
            if (row.dwOwningPid == pid)
                list << QStringLiteral("tcp state=%1 local=%2 remote=%3").arg(row.dwState).arg(portOf(row.dwLocalPort)).arg(portOf(row.dwRemotePort));
        }
    }
    size = 0;
    GetExtendedUdpTable(nullptr, &size, FALSE, AF_INET, UDP_TABLE_OWNER_PID, 0);
    buf.assign(size, 0);
    if (GetExtendedUdpTable(buf.data(), &size, FALSE, AF_INET, UDP_TABLE_OWNER_PID, 0) == NO_ERROR) {
        auto* table = reinterpret_cast<MIB_UDPTABLE_OWNER_PID*>(buf.data());
        for (DWORD i = 0; i < table->dwNumEntries; ++i) {
            const auto& row = table->table[i];
            if (row.dwOwningPid == pid)
                list << QStringLiteral("udp local=%1").arg(portOf(row.dwLocalPort));
        }
    }
    return list;
}

QStringList peImports(const QString& path)
{
    QStringList imports;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return imports;
    const QByteArray data = f.readAll();
    if (data.size() < static_cast<qsizetype>(sizeof(IMAGE_DOS_HEADER)))
        return imports;
    const auto* base = reinterpret_cast<const unsigned char*>(data.constData());
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew + static_cast<LONG>(sizeof(IMAGE_NT_HEADERS64)) > data.size())
        return imports;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        return imports;
    const IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (dir.VirtualAddress == 0)
        return imports;

    // RVA → 文件偏移
    const auto* sections = IMAGE_FIRST_SECTION(nt);
    auto rvaToOffset = [&](DWORD rva) -> qint64 {
        for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
            const auto& s = sections[i];
            if (rva >= s.VirtualAddress && rva < s.VirtualAddress + s.Misc.VirtualSize)
                return static_cast<qint64>(rva - s.VirtualAddress + s.PointerToRawData);
        }
        return -1;
    };
    const qint64 off = rvaToOffset(dir.VirtualAddress);
    if (off < 0)
        return imports;
    for (qint64 pos = off;; pos += sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
        if (pos + static_cast<qint64>(sizeof(IMAGE_IMPORT_DESCRIPTOR)) > data.size())
            break;
        const auto* desc = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + pos);
        if (desc->Name == 0)
            break;
        const qint64 nameOff = rvaToOffset(desc->Name);
        if (nameOff < 0 || nameOff >= data.size())
            break;
        imports << QString::fromLatin1(reinterpret_cast<const char*>(base + nameOff));
    }
    return imports;
}

} // namespace ss::win
