// windows.h 必须带 NOMINMAX / WIN32_LEAN_AND_MEAN 引入，否则 min/max 宏会污染后续头文件。
#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef _WIN32_WINNT
#    define _WIN32_WINNT 0x0601
#  endif
#  include <windows.h>
#endif

#include "systempaths.h"

#include <QDir>

namespace {

// 拼成 Windows 原生分隔符的路径（CreateProcess 对正斜杠不如反斜杠稳妥）
QString joinNative(const QString &directory, const QString &fileName)
{
    QString path = directory;
    const QChar last = path.isEmpty() ? QChar() : path.at(path.size() - 1);
    if (last != QLatin1Char('\\') && last != QLatin1Char('/'))
        path += QLatin1Char('\\');
    path += fileName;
    return QDir::toNativeSeparators(path);
}

} // namespace

QString system32Path(const QString &fileName)
{
    if (fileName.isEmpty())
        return QString();

#ifdef _WIN32
    wchar_t buffer[MAX_PATH] = {};
    const UINT length = GetSystemDirectoryW(buffer, MAX_PATH);
    if (length > 0 && length < UINT(MAX_PATH))
        return joinNative(QString::fromWCharArray(buffer, int(length)), fileName);

    // 兜底：环境变量 SystemRoot（同样是解析出来的路径，不是写死盘符），正常情况下走不到这里
    const QString root = qEnvironmentVariable("SystemRoot");
    if (!root.isEmpty())
        return joinNative(joinNative(root, QStringLiteral("System32")), fileName);
#endif

    return QString();
}
