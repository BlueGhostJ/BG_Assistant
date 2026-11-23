#include "chromemultiopen.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QSet>
#include <QSettings>

namespace {

// 与 lockscreen.cpp 用的是同一个配置文件（exe 同目录 BGAssistant.ini），只是分组不同：
// 锁屏在 [LockScreen]，Chrome 多开在 [ChromeMultiOpen]。
QString iniFileName()
{
    return QCoreApplication::applicationDirPath()
           + QDir::separator() + QStringLiteral("BGAssistant.ini");
}

const char kGroup[] = "ChromeMultiOpen";

// 统一成 Qt 风格的正斜杠路径，去掉尾部多余的分隔符：
// 这样 ini 里存的值、界面显示、拼进命令行三者只需要在显示/启动时各转一次。
QString normalizePath(const QString &path)
{
    QString clean = QDir::fromNativeSeparators(path.trimmed());
    if (clean.isEmpty())
        return clean;
    clean = QDir::cleanPath(clean);
    return clean;
}

// 去掉首尾空白与成对的双引号：从 bat 里复制过来的参数常带引号
QString stripped(QString text)
{
    text = text.trimmed();
    if (text.size() >= 2 && text.startsWith(QLatin1Char('"')) && text.endsWith(QLatin1Char('"')))
        text = text.mid(1, text.size() - 2).trimmed();
    return text;
}

bool needsQuote(const QString &value)
{
    return value.isEmpty() || value.contains(QLatin1Char(' '))
           || value.contains(QLatin1Char('\t')) || value.contains(QLatin1Char('"'));
}

// 命令行展示用：值里有空格就加引号，和 bat 里的写法一致，方便直接复制出去试
QString quoteForDisplay(const QString &value)
{
    if (!needsQuote(value))
        return value;
    QString escaped = value;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QStringLiteral("\"") + escaped + QStringLiteral("\"");
}

// Windows 文件名保留字：叫 CON 的目录建不出来
bool isReservedName(const QString &name)
{
    static const QStringList reserved{
        QStringLiteral("CON"),  QStringLiteral("PRN"),  QStringLiteral("AUX"),
        QStringLiteral("NUL"),  QStringLiteral("COM1"), QStringLiteral("COM2"),
        QStringLiteral("COM3"), QStringLiteral("COM4"), QStringLiteral("COM5"),
        QStringLiteral("COM6"), QStringLiteral("COM7"), QStringLiteral("COM8"),
        QStringLiteral("COM9"), QStringLiteral("LPT1"), QStringLiteral("LPT2"),
        QStringLiteral("LPT3"), QStringLiteral("LPT4"), QStringLiteral("LPT5"),
        QStringLiteral("LPT6"), QStringLiteral("LPT7"), QStringLiteral("LPT8"),
        QStringLiteral("LPT9"),
    };
    return reserved.contains(name.section(QLatin1Char('.'), 0, 0).toUpper());
}

} // namespace

QString ChromeMultiOpen::iniPath()
{
    return iniFileName();
}

QStringList ChromeMultiOpen::chromeCandidates()
{
    QStringList candidates;

    // 用户级安装（原 bat 脚本里写死的位置）：%LOCALAPPDATA%\Google\Chrome\Application\chrome.exe
    const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    if (!localAppData.isEmpty()) {
        candidates << QDir(localAppData).filePath(
            QStringLiteral("Google/Chrome/Application/chrome.exe"));
    }

    // 系统级安装：Program Files 系列变量都试一遍（64 位系统上 ProgramFiles 与 ProgramW6432 通常相同）
    const char *const roots[] = {"ProgramFiles", "ProgramFiles(x86)", "ProgramW6432"};
    for (const char *variable : roots) {
        const QString root = qEnvironmentVariable(variable);
        if (root.isEmpty())
            continue;
        candidates << QDir(root).filePath(QStringLiteral("Google/Chrome/Application/chrome.exe"));
    }

    // 去重并统一路径写法
    QStringList unique;
    QSet<QString> seen;
    for (const QString &candidate : candidates) {
        const QString path = normalizePath(candidate);
        const QString key = path.toLower();
        if (path.isEmpty() || seen.contains(key))
            continue;
        seen.insert(key);
        unique << path;
    }
    return unique;
}

QString ChromeMultiOpen::chromePath()
{
    for (const QString &candidate : chromeCandidates()) {
        if (QFileInfo(candidate).isFile())
            return candidate;
    }
    return QString();
}

QString ChromeMultiOpen::defaultDataRoot()
{
    return normalizePath(QCoreApplication::applicationDirPath() + QStringLiteral("/chrome多开"));
}

QString ChromeMultiOpen::dataRoot()
{
    QSettings ini(iniFileName(), QSettings::IniFormat);
    const QString stored = ini.value(QString::fromLatin1(kGroup) + QStringLiteral("/DataDir"))
                               .toString()
                               .trimmed();
    if (stored.isEmpty())
        return defaultDataRoot();
    return normalizePath(stored);
}

void ChromeMultiOpen::setDataRoot(const QString &path)
{
    QSettings ini(iniFileName(), QSettings::IniFormat);
    const QString key = QString::fromLatin1(kGroup) + QStringLiteral("/DataDir");
    const QString clean = normalizePath(path);
    if (clean.isEmpty())
        ini.remove(key);   // 空值 = 恢复默认（跟着 exe 走）
    else
        ini.setValue(key, clean);
    ini.sync();
}

QString ChromeMultiOpen::dataDirFor(const QString &name)
{
    return QDir(dataRoot()).filePath(name.trimmed());
}

QString ChromeMultiOpen::tempDirFor(const QString &name)
{
    // 与 bat 里的 %temp%\%batFileName% 一致：浏览器缓存单独放系统临时目录，
    // 不混进多开数据目录，拷走数据目录时也不带上几百 MB 缓存
    return QDir(QDir::tempPath()).filePath(name.trimmed());
}

QList<ChromeProfile> ChromeMultiOpen::loadProfiles()
{
    QList<ChromeProfile> profiles;

    QSettings ini(iniFileName(), QSettings::IniFormat);
    ini.beginGroup(QString::fromLatin1(kGroup));
    const int count = ini.beginReadArray(QStringLiteral("Profiles"));
    for (int i = 0; i < count; ++i) {
        ini.setArrayIndex(i);

        ChromeProfile profile;
        profile.name = ini.value(QStringLiteral("Name")).toString().trimmed();
        if (profile.name.isEmpty())
            continue;   // 手工改坏的条目直接跳过

        profile.proxyMode = ini.value(QStringLiteral("ProxyMode"),
                                      int(ChromeProfile::ProxySystem))
                                .toInt();
        if (profile.proxyMode < ChromeProfile::ProxySystem
            || profile.proxyMode > ChromeProfile::ProxyDisabled) {
            profile.proxyMode = ChromeProfile::ProxySystem;
        }
        profile.proxyServer = ini.value(QStringLiteral("ProxyServer")).toString();

        profile.useUserAgent = ini.value(QStringLiteral("UseUserAgent"), false).toBool();
        profile.userAgent = ini.value(QStringLiteral("UserAgent")).toString();

        profile.useAppMode = ini.value(QStringLiteral("UseAppMode"), false).toBool();
        profile.appUrl = ini.value(QStringLiteral("AppUrl")).toString();

        profile.useWindowSize = ini.value(QStringLiteral("UseWindowSize"), false).toBool();
        profile.windowWidth = ini.value(QStringLiteral("WindowWidth"), 1280).toInt();
        profile.windowHeight = ini.value(QStringLiteral("WindowHeight"), 720).toInt();

        profile.useWindowPosition = ini.value(QStringLiteral("UseWindowPosition"), false).toBool();
        profile.windowX = ini.value(QStringLiteral("WindowX"), 0).toInt();
        profile.windowY = ini.value(QStringLiteral("WindowY"), 0).toInt();

        profile.startMaximized = ini.value(QStringLiteral("StartMaximized"), false).toBool();
        profile.startFullscreen = ini.value(QStringLiteral("StartFullscreen"), false).toBool();
        profile.kiosk = ini.value(QStringLiteral("Kiosk"), false).toBool();
        profile.forceScale150 = ini.value(QStringLiteral("ForceScale150"), false).toBool();

        profile.useWindowName = ini.value(QStringLiteral("UseWindowName"), false).toBool();
        profile.windowName = ini.value(QStringLiteral("WindowName")).toString();

        profiles.append(profile);
    }
    ini.endArray();
    ini.endGroup();

    return profiles;
}

bool ChromeMultiOpen::saveProfiles(const QList<ChromeProfile> &profiles)
{
    QSettings ini(iniFileName(), QSettings::IniFormat);
    ini.beginGroup(QString::fromLatin1(kGroup));

    // 整个数组重写：改名、删除、调整顺序后不会留下旧键
    ini.remove(QStringLiteral("Profiles"));
    ini.beginWriteArray(QStringLiteral("Profiles"), int(profiles.size()));
    for (int i = 0; i < profiles.size(); ++i) {
        ini.setArrayIndex(i);
        const ChromeProfile &profile = profiles.at(i);

        ini.setValue(QStringLiteral("Name"), profile.name);
        ini.setValue(QStringLiteral("ProxyMode"), profile.proxyMode);
        ini.setValue(QStringLiteral("ProxyServer"), profile.proxyServer);
        ini.setValue(QStringLiteral("UseUserAgent"), profile.useUserAgent);
        ini.setValue(QStringLiteral("UserAgent"), profile.userAgent);
        ini.setValue(QStringLiteral("UseAppMode"), profile.useAppMode);
        ini.setValue(QStringLiteral("AppUrl"), profile.appUrl);
        ini.setValue(QStringLiteral("UseWindowSize"), profile.useWindowSize);
        ini.setValue(QStringLiteral("WindowWidth"), profile.windowWidth);
        ini.setValue(QStringLiteral("WindowHeight"), profile.windowHeight);
        ini.setValue(QStringLiteral("UseWindowPosition"), profile.useWindowPosition);
        ini.setValue(QStringLiteral("WindowX"), profile.windowX);
        ini.setValue(QStringLiteral("WindowY"), profile.windowY);
        ini.setValue(QStringLiteral("StartMaximized"), profile.startMaximized);
        ini.setValue(QStringLiteral("StartFullscreen"), profile.startFullscreen);
        ini.setValue(QStringLiteral("Kiosk"), profile.kiosk);
        ini.setValue(QStringLiteral("ForceScale150"), profile.forceScale150);
        ini.setValue(QStringLiteral("UseWindowName"), profile.useWindowName);
        ini.setValue(QStringLiteral("WindowName"), profile.windowName);
    }
    ini.endArray();
    ini.endGroup();
    ini.sync();

    // 程序目录只读时这里会失败，交给调用方提示用户
    return ini.status() == QSettings::NoError;
}

QString ChromeMultiOpen::proxySummary(const ChromeProfile &profile)
{
    switch (profile.proxyMode) {
    case ChromeProfile::ProxyCustom:
        return profile.proxyServer.trimmed().isEmpty() ? QStringLiteral("系统默认")
                                                       : profile.proxyServer.trimmed();
    case ChromeProfile::ProxyDisabled:
        return QStringLiteral("不使用代理");
    default:
        return QStringLiteral("系统默认");
    }
}

QString ChromeMultiOpen::optionSummary(const ChromeProfile &profile)
{
    QStringList parts;

    if (profile.useAppMode && !profile.appUrl.trimmed().isEmpty())
        parts << QStringLiteral("独立窗口");
    if (profile.useWindowSize)
        parts << QStringLiteral("%1×%2").arg(profile.windowWidth).arg(profile.windowHeight);
    if (profile.useWindowPosition)
        parts << QStringLiteral("位置 %1,%2").arg(profile.windowX).arg(profile.windowY);

    // 三个"开窗方式"在 Chrome 里互相覆盖，实际只有优先级最高的那个生效
    if (profile.kiosk)
        parts << QStringLiteral("演示全屏");
    else if (profile.startFullscreen)
        parts << QStringLiteral("强制全屏");
    else if (profile.startMaximized)
        parts << QStringLiteral("最大化");

    if (profile.forceScale150)
        parts << QStringLiteral("缩放 150%");
    if (profile.useUserAgent && !profile.userAgent.trimmed().isEmpty())
        parts << QStringLiteral("自定义 UA");
    if (profile.useWindowName && !profile.windowName.trimmed().isEmpty())
        parts << QStringLiteral("窗口名：%1").arg(profile.windowName.trimmed());

    return parts.isEmpty() ? QStringLiteral("默认窗口") : parts.join(QStringLiteral(" · "));
}

QStringList ChromeMultiOpen::buildArguments(const ChromeProfile &profile)
{
    QStringList arguments;

    // 必带的两项：独立数据目录 + 独立缓存目录（对应 bat 脚本里的两个 --*-dir）
    arguments << QStringLiteral("--user-data-dir=")
                     + QDir::toNativeSeparators(dataDirFor(profile.name))
              << QStringLiteral("--disk-cache-dir=")
                     + QDir::toNativeSeparators(tempDirFor(profile.name));

    // 代理：不选就是系统默认，此时一个参数都不传
    const QString proxyServer = stripped(profile.proxyServer);
    if (profile.proxyMode == ChromeProfile::ProxyCustom && !proxyServer.isEmpty())
        arguments << QStringLiteral("--proxy-server=") + proxyServer;
    else if (profile.proxyMode == ChromeProfile::ProxyDisabled)
        arguments << QStringLiteral("--no-proxy-server");

    const QString userAgent = stripped(profile.userAgent);
    if (profile.useUserAgent && !userAgent.isEmpty())
        arguments << QStringLiteral("--user-agent=") + userAgent;

    QString appUrl = stripped(profile.appUrl);
    if (profile.useAppMode && !appUrl.isEmpty()) {
        // 没写协议时按 https 补全：--app=example.com 这种写法 Chrome 不认
        if (!appUrl.contains(QStringLiteral("://")))
            appUrl.prepend(QStringLiteral("https://"));
        arguments << QStringLiteral("--app=") + appUrl;
    }

    if (profile.useWindowSize)
        arguments << QStringLiteral("--window-size=%1,%2").arg(profile.windowWidth).arg(profile.windowHeight);

    if (profile.useWindowPosition)
        arguments << QStringLiteral("--window-position=%1,%2").arg(profile.windowX).arg(profile.windowY);

    // 与 optionSummary 的优先级一致：演示 > 强制全屏 > 最大化
    if (profile.kiosk)
        arguments << QStringLiteral("--kiosk");
    else if (profile.startFullscreen)
        arguments << QStringLiteral("--start-fullscreen");
    else if (profile.startMaximized)
        arguments << QStringLiteral("--start-maximized");

    if (profile.forceScale150)
        arguments << QStringLiteral("--force-device-scale-factor=1.5");

    const QString windowName = stripped(profile.windowName);
    if (profile.useWindowName && !windowName.isEmpty())
        arguments << QStringLiteral("--window-name=") + windowName;

    return arguments;
}

QString ChromeMultiOpen::commandLine(const ChromeProfile &profile)
{
    QString exe = chromePath();
    if (exe.isEmpty())
        exe = QStringLiteral("chrome.exe");

    QStringList parts;
    parts << quoteForDisplay(QDir::toNativeSeparators(exe));

    for (const QString &argument : buildArguments(profile)) {
        const int equals = argument.indexOf(QLatin1Char('='));
        if (equals <= 0) {
            parts << argument;
            continue;
        }

        const QString flag = argument.left(equals + 1);
        const QString value = argument.mid(equals + 1);
        // 带空格的值加引号显示（bat 里也是这么写的），复制到 cmd 里能直接跑
        parts << (needsQuote(value) ? flag + quoteForDisplay(value) : argument);
    }

    return parts.join(QLatin1Char(' '));
}

bool ChromeMultiOpen::isValidName(const QString &name, QString *reason)
{
    if (reason)
        reason->clear();

    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        if (reason)
            *reason = QStringLiteral("名称不能为空。");
        return false;
    }

    if (trimmed.size() > 64) {
        if (reason)
            *reason = QStringLiteral("名称太长，请控制在 64 个字符以内。");
        return false;
    }

    // 名称会直接变成数据子目录名，Windows 文件名不允许的字符必须挡掉
    static const QString invalidChars = QStringLiteral("\\/:*?\"<>|");
    for (const QChar ch : trimmed) {
        if (invalidChars.contains(ch)) {
            if (reason) {
                *reason = QStringLiteral("名称不能包含 \\ / : * ? \" < > | 这些字符"
                                         "（它会作为数据目录名使用）。");
            }
            return false;
        }
    }

    if (trimmed.endsWith(QLatin1Char('.')) || trimmed.endsWith(QLatin1Char(' '))) {
        if (reason)
            *reason = QStringLiteral("名称不能以点号或空格结尾。");
        return false;
    }

    if (isReservedName(trimmed)) {
        if (reason)
            *reason = QStringLiteral("「%1」是 Windows 保留名称，请换一个。").arg(trimmed);
        return false;
    }

    return true;
}

bool ChromeMultiOpen::launch(const ChromeProfile &profile, QString *error)
{
    if (error)
        error->clear();

    if (profile.name.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("这个多开没有名称，无法确定数据目录。");
        return false;
    }

    const QString exe = chromePath();
    if (exe.isEmpty()) {
        if (error) {
            *error = QStringLiteral("没有找到 chrome.exe，无法启动多开。\n\n已查找以下位置：\n%1")
                         .arg(chromeCandidates().join(QLatin1Char('\n')));
        }
        return false;
    }

    // 与脚本里的 md 一样先把目录建好；数据目录建不出来就没法继续
    const QString dataDir = dataDirFor(profile.name);
    if (!QDir().mkpath(dataDir)) {
        if (error) {
            *error = QStringLiteral("无法创建多开数据目录：\n%1\n\n"
                                    "请检查该位置是否可写，或在「多开管理」里换一个数据位置。")
                         .arg(QDir::toNativeSeparators(dataDir));
        }
        return false;
    }

    // 缓存目录建不出来不致命：Chrome 自己也会建
    QDir().mkpath(tempDirFor(profile.name));

    // 普通权限直接起进程即可，不需要像路由/端口转发那样提权。
    // 路径统一转成反斜杠：CreateProcess 对正斜杠也能处理，但没必要在这里赌。
    if (!QProcess::startDetached(QDir::toNativeSeparators(exe), buildArguments(profile))) {
        if (error) {
            *error = QStringLiteral("启动 chrome.exe 失败：\n%1\n\n"
                                    "请确认 Chrome 没有被策略禁止运行。")
                         .arg(QDir::toNativeSeparators(exe));
        }
        return false;
    }

    return true;
}
