#pragma once

// 测试公共工具：图像比对断言、失败产物目录
#include "testing/ImageCompare.h"

#include <QDir>
#include <QStandardPaths>
#include <QTest>

namespace ss::testing {

inline QString failureDir()
{
    const QByteArray env = qgetenv("SS_FAILURE_DIR");
    if (!env.isEmpty())
        return QString::fromLocal8Bit(env);
    return QDir::temp().filePath(QStringLiteral("screenshot-test-failures"));
}

} // namespace ss::testing

// 逐像素比对断言（TST-04）：失败时写出实际图、期望图和差异图
#define SS_VERIFY_IMAGE(actual, expected, options, name)                                                              \
    do {                                                                                                              \
        const auto _r = ss::testing::compareImages((actual), (expected), (options));                                  \
        if (!_r.passed) {                                                                                             \
            ss::testing::saveFailureArtifacts(ss::testing::failureDir(), QStringLiteral(name), (actual), (expected)); \
            QFAIL(qPrintable(QStringLiteral("图像比对失败 [%1]：%2").arg(QStringLiteral(name), _r.summary())));     \
        }                                                                                                             \
    } while (false)
