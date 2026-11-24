#include "appconfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace {

const char kIniName[] = "BGAssistant.ini";

// 统一成 Qt 风格的正斜杠路径，便于和 QSettings / QFileInfo 打交道
QString normalized(const QString &path)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(path));
}

} // namespace

QString AppConfig::installDataDir()
{
    // 正常情况就用 LOCALAPPDATA（%USERPROFILE%\AppData\Local）；
    // 极端情况下变量被删了，按 Windows 的默认布局拼一个出来
    QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    if (localAppData.isEmpty())
        localAppData = QDir::homePath() + QStringLiteral("/AppData/Local");

    return normalized(QDir(localAppData).filePath(QStringLiteral("Programs/BGAssistant")));
}

QString AppConfig::exeDirIniPath()
{
    return normalized(QDir(QCoreApplication::applicationDirPath())
                          .filePath(QString::fromLatin1(kIniName)));
}

QString AppConfig::iniPath()
{
    // 只看文件在不在：exe 同目录有 ini 就是绿色版布局，没有就用程序数据目录
    const QString exeIni = exeDirIniPath();
    if (QFileInfo::exists(exeIni))
        return exeIni;

    return normalized(QDir(installDataDir()).filePath(QString::fromLatin1(kIniName)));
}

bool AppConfig::ensureIniDir(QString *error)
{
    if (error)
        error->clear();

    const QString dir = QFileInfo(iniPath()).absolutePath();
    if (QDir().mkpath(dir))
        return true;

    if (error)
        *error = dir;
    return false;
}
