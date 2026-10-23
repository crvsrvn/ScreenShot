#pragma once

#include <QList>
#include <QString>
#include <QtGlobal>

namespace ss::win {

// 进程私有内存（字节），pid 为 0 表示当前进程（NFR-02、R-08）
quint64 privateBytes(quint32 pid = 0);

// 进程当前的 TCP / UDP 端点描述（TST-09 运行期零网络监控）；空表示没有任何网络端点
QStringList networkEndpoints(quint32 pid);

// PE 文件导入的 DLL 名称列表（R-17、TST-09 构建产物检查）
QStringList peImports(const QString& path);

} // namespace ss::win
