#include "sandboxie.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStringConverter>
#include <QStringDecoder>

#include <algorithm>
#include <utility>

namespace {

// 去掉首尾空白与成对的双引号：ini 里的路径常写成 "D:\x\y.exe"
QString stripQuotes(QString text)
{
    text = text.trimmed();
    if (text.size() >= 2 && text.startsWith(QLatin1Char('"')) && text.endsWith(QLatin1Char('"')))
        text = text.mid(1, text.size() - 2).trimmed();
    return text;
}

// [GlobalSettings] / [UserSettings_xxx] 里的 RunCommand 不是沙箱，跳过
bool isGlobalSection(const QString &name)
{
    return name.startsWith(QLatin1String("GlobalSettings"), Qt::CaseInsensitive)
           || name.startsWith(QLatin1String("UserSettings"), Qt::CaseInsensitive);
}

// Sandboxie 默认把 ini 写成 UTF-16 LE（带 BOM），但手工改过的文件可能是
// UTF-16 BE 或不带 BOM 的 UTF-8 / 本地编码，这里统一兜住。
QString decodeIni(const QByteArray &raw)
{
    const auto *bytes = reinterpret_cast<const uchar *>(raw.constData());
    const qsizetype size = raw.size();

    if (size >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) {
        return QString::fromUtf16(reinterpret_cast<const char16_t *>(raw.constData() + 2),
                                  (size - 2) / 2);
    }

    if (size >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF) {
        // UTF-16 BE：逐对交换字节后按 LE 解，免得多引一个转换器
        QByteArray swapped = raw.mid(2);
        if (swapped.size() % 2 != 0)
            swapped.chop(1);
        char *data = swapped.data();
        for (qsizetype i = 0; i + 1 < swapped.size(); i += 2)
            std::swap(data[i], data[i + 1]);
        return QString::fromUtf16(reinterpret_cast<const char16_t *>(data), swapped.size() / 2);
    }

    // 无 BOM 的 UTF-16 LE：纯 ASCII 文本每隔一个字节就是 0，据此判断
    const qsizetype probe = qMin<qsizetype>(size, 512);
    if (probe >= 8) {
        qsizetype zeros = 0;
        for (qsizetype i = 1; i < probe; i += 2) {
            if (raw.at(i) == 0)
                ++zeros;
        }
        if (zeros * 4 >= probe)
            return QString::fromUtf16(reinterpret_cast<const char16_t *>(raw.constData()),
                                      size / 2);
    }

    QStringDecoder utf8(QStringConverter::Utf8);
    QString text = utf8.decode(raw);
    if (utf8.hasError()) {
        // 既不是合法 UTF-8，按系统本地编码（简中环境即 GBK）再解一次
        QStringDecoder local(QStringConverter::System);
        text = local.decode(raw);
    }
    return text;
}

} // namespace

QString Sandboxie::iniPath()
{
    return QStringLiteral("C:\\Windows\\Sandboxie.ini");
}

QString Sandboxie::sandManPath()
{
    // 安装在非默认盘符时靠 ProgramFiles 系列变量兜住；都找不到就先返回默认值，
    // 由 launch() 在真正启动时把「找不到」报出来，避免菜单里静默失效
    const QStringList roots{
        qEnvironmentVariable("ProgramW6432"),
        qEnvironmentVariable("ProgramFiles"),
        qEnvironmentVariable("ProgramFiles(x86)"),
    };

    for (const QString &root : roots) {
        if (root.isEmpty())
            continue;
        for (const QString &folder : {QStringLiteral("Sandboxie-Plus"), QStringLiteral("Sandboxie")}) {
            const QString candidate = QDir(root).filePath(folder + QStringLiteral("/SandMan.exe"));
            if (QFileInfo::exists(candidate))
                return QDir::toNativeSeparators(candidate);
        }
    }

    return QStringLiteral("C:\\Program Files\\Sandboxie-Plus\\SandMan.exe");
}

QString Sandboxie::mapPath(const QString &rawPath)
{
    QString path = stripQuotes(rawPath);
    if (path.isEmpty())
        return QString();

    // 统一成反斜杠，后面匹配前缀时只需要考虑一种写法
    path.replace(QLatin1Char('/'), QLatin1Char('\\'));

    // %BoxRoot%\drive\<盘符>\<其余> 是沙箱里 <盘符>: 盘文件的落盘位置，反向映射回 <盘符>:\<其余>
    static const QRegularExpression driveRe(
        QStringLiteral("^%BoxRoot%\\\\drive\\\\([A-Za-z])(?:\\\\(.*))?$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch driveMatch = driveRe.match(path);
    if (driveMatch.hasMatch()) {
        const QString drive = driveMatch.captured(1).toUpper();
        const QString rest = driveMatch.captured(2);
        return QDir::toNativeSeparators(drive + QStringLiteral(":\\") + rest);
    }

    // %BoxRoot%\user\current 是沙箱内的当前用户配置目录，对应真实的用户目录
    static const QRegularExpression userRe(
        QStringLiteral("^%BoxRoot%\\\\user\\\\current(?:\\\\(.*))?$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch userMatch = userRe.match(path);
    if (userMatch.hasMatch()) {
        // 这条正则只有一个捕获组（用户目录之后的部分），不要顺手写成 2
        const QString rest = userMatch.captured(1);
        const QString home = QDir::toNativeSeparators(QDir::homePath());
        return rest.isEmpty() ? home
                              : QDir::toNativeSeparators(home + QLatin1Char('\\') + rest);
    }

    // 不带 %BoxRoot% 的写法本身就是真实路径；其它认不出的 %BoxRoot% 子布局原样返回
    // （%BoxRoot% 是 Sandboxie 自己的变量，交给它展开）
    return QDir::toNativeSeparators(path);
}

QList<SandboxEntry> Sandboxie::loadEntries(const QString &iniPath)
{
    QList<SandboxEntry> entries;

    QFile file(iniPath);
    if (!file.open(QIODevice::ReadOnly))
        return entries;   // 文件不存在或没权限：当作没有沙箱，菜单直接不显示
    const QString text = decodeIni(file.readAll());
    file.close();

    QString section;
    // 小写节名 → 该节的 RunCommand 条数：决定菜单项要不要带上键名
    QHash<QString, int> commandsPerBox;
    // 同节 / 同键 / 同路径的重复行只留一条，避免菜单里出现两个一模一样的项
    QSet<QString> seenLines;

    const QStringList lines =
        text.split(QRegularExpression(QStringLiteral("[\r\n]")), Qt::SkipEmptyParts);

    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char(';')))
            continue;   // 注释行（Sandboxie 用 # 和 ;）

        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            section = line.mid(1, line.size() - 2).trimmed();
            continue;
        }

        if (section.isEmpty() || isGlobalSection(section))
            continue;

        const qsizetype equals = line.indexOf(QLatin1Char('='));
        if (equals <= 0)
            continue;

        if (line.left(equals).trimmed().compare(QLatin1String("RunCommand"),
                                               Qt::CaseInsensitive) != 0)
            continue;

        QString value = line.mid(equals + 1).trimmed();
        QString program;
        const qsizetype bar = value.indexOf(QLatin1Char('|'));
        if (bar >= 0) {
            program = stripQuotes(value.left(bar));
            value = value.mid(bar + 1);
        }

        const QString rawPath = stripQuotes(value);
        if (rawPath.isEmpty())
            continue;

        const QString boxKey = section.toLower();
        const QString lineKey = boxKey + QLatin1Char('\n') + program.toLower()
                                + QLatin1Char('\n') + rawPath.toLower();
        if (seenLines.contains(lineKey))
            continue;
        seenLines.insert(lineKey);

        SandboxEntry entry;
        entry.boxName = section;
        entry.program = program;
        entry.rawPath = rawPath;
        entry.targetPath = mapPath(rawPath);
        entry.commandLine = QStringLiteral("\"%1\" /box:%2 \"%3\"")
                                .arg(sandManPath(), entry.boxName, entry.targetPath);

        commandsPerBox[boxKey] += 1;
        entries.append(entry);
    }

    // 一个节里有多条 RunCommand 时，只用沙箱名会让菜单里出现多个同名项，
    // 这时改成「沙箱名|键名」；键名缺失（RunCommand 只写了路径）就退回可执行文件名。
    for (SandboxEntry &entry : entries) {
        if (commandsPerBox.value(entry.boxName.toLower()) <= 1) {
            entry.menuTitle = entry.boxName;
            continue;
        }

        QString key = entry.program;
        if (key.isEmpty())
            key = QFileInfo(entry.targetPath).fileName();
        if (key.isEmpty())
            key = QStringLiteral("RunCommand");
        entry.menuTitle = entry.boxName + QLatin1Char('|') + key;
    }

    // 字母序（忽略大小写）：先按沙箱名，同一沙箱的多条再按键名。
    // 用 stable_sort，名字完全相同（仅大小写不同）时保持 ini 里的先后顺序。
    std::stable_sort(entries.begin(), entries.end(),
                     [](const SandboxEntry &a, const SandboxEntry &b) {
                         const int byBox = a.boxName.compare(b.boxName, Qt::CaseInsensitive);
                         if (byBox != 0)
                             return byBox < 0;
                         return a.menuTitle.compare(b.menuTitle, Qt::CaseInsensitive) < 0;
                     });

    return entries;
}

bool Sandboxie::launch(const SandboxEntry &entry, QString *errorOut)
{
    if (errorOut)
        errorOut->clear();

    const QString exe = sandManPath();
    if (!QFileInfo::exists(exe)) {
        if (errorOut) {
            *errorOut = QStringLiteral("找不到 SandMan.exe：\n%1\n\n请确认 Sandboxie-Plus 已安装。").arg(exe);
        }
        return false;
    }

    // 参数拆成列表交给 Qt 拼命令行，路径里的空格不用自己转义；
    // 展示用的 entry.commandLine 与这里的两个参数一一对应。
    const QStringList arguments{QStringLiteral("/box:") + entry.boxName, entry.targetPath};
    if (!QProcess::startDetached(exe, arguments)) {
        if (errorOut)
            *errorOut = QStringLiteral("启动沙箱「%1」失败：\n%2").arg(entry.boxName, entry.commandLine);
        return false;
    }

    return true;
}
