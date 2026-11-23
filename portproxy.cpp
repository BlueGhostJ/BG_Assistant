// 端口转发的系统层封装：读写 netsh interface portproxy 的 v4tov4 表。
//
// 关于「编辑」：netsh 其实有 set v4tov4，但它只能更新连接地址/端口，
// 表项的主键（侦听地址 + 侦听端口）没法通过 set 改；而且网上有 set 只写进了配置、
// 没让 IP Helper 重新监听（netstat 看不到端口在听）的实例。
// 所以这里只暴露 add / delete，编辑由界面层按「先删旧、再加新」实现：
// 每次 add 都会走一遍完整的建表 + 绑定监听流程，行为确定、立即生效。

// Windows 头必须先于 Qt 引入（和 routequery.cpp 一致）：带 NOMINMAX / WIN32_LEAN_AND_MEAN，
// 免得 min/max 宏污染后面的 Qt 头。这里还要 winsvc.h 查 IP Helper 服务状态。
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
#  include <winsvc.h>
#endif

#include "portproxy.h"

#include "elevation.h"

#include <QFileInfo>
#include <QProcess>

#include <algorithm>

#ifndef CREATE_NO_WINDOW
#  define CREATE_NO_WINDOW 0x08000000
#endif

namespace {

// 侦听地址排序用：把 IPv4 转成整数，非法值排在最前
quint32 ipv4Key(const QString &ip)
{
    const QStringList parts = ip.split(QLatin1Char('.'));
    if (parts.size() != 4)
        return 0;

    quint32 key = 0;
    for (const QString &part : parts) {
        bool ok = false;
        const uint value = part.toUInt(&ok);
        if (!ok || value > 255)
            return 0;
        key = (key << 8) | value;
    }
    return key;
}

} // namespace

bool queryPortProxies(QVector<PortProxyEntry> *entries, QString *error)
{
    if (!entries)
        return false;

    entries->clear();

    QStringList arguments;
    arguments << QStringLiteral("interface") << QStringLiteral("portproxy")
              << QStringLiteral("show") << QStringLiteral("v4tov4");

    const PortProxyResult result = runPortProxyCommand(arguments);
    if (!result.started) {
        if (error)
            *error = result.output;
        return false;
    }
    if (result.exitCode != 0) {
        if (error) {
            *error = result.output.isEmpty()
                         ? QStringLiteral("读取端口转发列表失败（netsh 返回 %1）。").arg(result.exitCode)
                         : result.output;
        }
        return false;
    }

    // show v4tov4 的表格列宽固定，数据行形如：
    //   192.168.81.1    7890        127.0.0.1       7890
    // 表头是本地化文字（「地址 端口 地址 端口」），按「4 个字段且第 2、4 个是端口号」来筛，
    // 不依赖任何语言，也不怕列宽变化。
    const QStringList lines = result.output.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QStringList fields = line.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (fields.size() != 4)
            continue;

        bool listenOk = false;
        bool connectOk = false;
        const int listenPort = fields.at(1).toInt(&listenOk);
        const int connectPort = fields.at(3).toInt(&connectOk);
        if (!listenOk || !connectOk)
            continue;
        if (listenPort < 1 || listenPort > 65535 || connectPort < 1 || connectPort > 65535)
            continue;

        PortProxyEntry entry;
        // 部分 Windows 版本把「所有网卡」显示成 *，而 * 和 0.0.0.0 在 netsh 里是两条不同的表项：
        // 表单统一按 0.0.0.0 显示，原始写法留在 storedListenAddress 里，删除时原样还回去。
        entry.storedListenAddress = fields.at(0);
        entry.listenAddress = fields.at(0) == QLatin1String("*") ? QStringLiteral("0.0.0.0")
                                                                : fields.at(0);
        entry.listenPort = quint16(listenPort);
        entry.connectAddress = fields.at(2);
        entry.connectPort = quint16(connectPort);
        entries->append(entry);
    }

    // netsh 的输出顺序跟着注册表走，刷新几次可能变；固定按侦听地址、端口排，便于对照
    std::sort(entries->begin(), entries->end(), [](const PortProxyEntry &a, const PortProxyEntry &b) {
        const quint32 keyA = ipv4Key(a.listenAddress);
        const quint32 keyB = ipv4Key(b.listenAddress);
        if (keyA != keyB)
            return keyA < keyB;
        if (a.listenPort != b.listenPort)
            return a.listenPort < b.listenPort;
        if (a.connectAddress != b.connectAddress)
            return a.connectAddress < b.connectAddress;
        return a.connectPort < b.connectPort;
    });

    return true;
}

QStringList portProxyArguments(PortProxyAction action, const PortProxyEntry &entry)
{
    // 删除要按 netsh 里存的原样写法回填：用 * 建的规则只能按 * 删，用 0.0.0.0 建的只能按 0.0.0.0 删
    const QString listenAddress =
        action == PortProxyAction::Delete && !entry.storedListenAddress.isEmpty()
            ? entry.storedListenAddress
            : entry.listenAddress;

    QStringList arguments;
    arguments << QStringLiteral("interface") << QStringLiteral("portproxy")
              << (action == PortProxyAction::Add ? QStringLiteral("add") : QStringLiteral("delete"))
              << QStringLiteral("v4tov4")
              << QStringLiteral("listenaddress=%1").arg(listenAddress)
              << QStringLiteral("listenport=%1").arg(int(entry.listenPort));

    if (action == PortProxyAction::Add) {
        arguments << QStringLiteral("connectaddress=%1").arg(entry.connectAddress)
                  << QStringLiteral("connectport=%1").arg(int(entry.connectPort));
    }
    return arguments;
}

PortProxyResult runPortProxyCommand(const QStringList &arguments)
{
    PortProxyResult result;

    // 必须用 System32 的绝对路径：本窗口跑在提权子进程里，
    // 同目录放一个假的 netsh.exe 就等于把管理员权限交了出去。
    const QString netshExe = system32Path(QStringLiteral("netsh.exe"));
    if (netshExe.isEmpty() || !QFileInfo::exists(netshExe)) {
        result.output = QStringLiteral("找不到系统目录下的 netsh.exe，无法执行端口转发命令。");
        return result;
    }

    QProcess process;
    process.setProgram(netshExe);
    process.setArguments(arguments);
#ifdef Q_OS_WIN
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif
    process.start();

    if (!process.waitForStarted(5000)) {
        result.output = process.errorString();
        return result;
    }
    if (!process.waitForFinished(15000)) {
        process.kill();
        process.waitForFinished(1000);
        result.output = QStringLiteral("命令执行超时。");
        return result;
    }

    result.started = true;
    result.exitCode = process.exitCode();
    // netsh 的中文输出是控制台本地编码（GBK），不能按 UTF-8 解
    result.output = QString::fromLocal8Bit(process.readAllStandardOutput()
                                           + process.readAllStandardError())
                        .trimmed();
    return result;
}

bool samePortProxyEndpoint(const PortProxyEntry &a, const PortProxyEntry &b)
{
    // 地址只可能是点分 IPv4 或主机名，大小写不敏感地比；* 与 0.0.0.0 视为同一个侦听端点
    const auto normalize = [](const QString &address) {
        return address == QLatin1String("*") ? QStringLiteral("0.0.0.0") : address;
    };

    return a.listenPort == b.listenPort
           && normalize(a.listenAddress).compare(normalize(b.listenAddress), Qt::CaseInsensitive) == 0;
}

IpHelperState ipHelperState()
{
#ifdef _WIN32
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager)
        return IpHelperState::Unknown;

    SC_HANDLE service = OpenServiceW(manager, L"iphlpsvc", SERVICE_QUERY_STATUS);
    if (!service) {
        CloseServiceHandle(manager);
        return IpHelperState::Unknown;
    }

    SERVICE_STATUS status = {};
    const BOOL ok = QueryServiceStatus(service, &status);
    CloseServiceHandle(service);
    CloseServiceHandle(manager);

    if (!ok)
        return IpHelperState::Unknown;
    return status.dwCurrentState == SERVICE_RUNNING ? IpHelperState::Running : IpHelperState::Stopped;
#else
    return IpHelperState::Unknown;
#endif
}

QString portProxyText(const PortProxyEntry &entry)
{
    return QStringLiteral("%1:%2 → %3:%4")
        .arg(entry.listenAddress)
        .arg(int(entry.listenPort))
        .arg(entry.connectAddress)
        .arg(int(entry.connectPort));
}
