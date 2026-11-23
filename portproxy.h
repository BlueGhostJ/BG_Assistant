#ifndef PORTPROXY_H
#define PORTPROXY_H

#include <QString>
#include <QStringList>
#include <QVector>

// 一条 IPv4 → IPv4 端口转发，对应 netsh interface portproxy 的 v4tov4 表项。
// 表项的唯一标识是「侦听地址 + 侦听端口」，连接地址/端口只是它的取值。
struct PortProxyEntry
{
    QString listenAddress;      // 侦听地址：本机的一个 IPv4 地址，0.0.0.0 表示所有网卡
    quint16 listenPort = 0;     // 侦听端口（1-65535）
    QString connectAddress;     // 转发目标地址
    quint16 connectPort = 0;    // 转发目标端口（1-65535）

    // netsh 表项里实际存的侦听地址写法。* 和 0.0.0.0 在 netsh 里是两条不同的表项，
    // 删除时必须原样回填，所以从 show 里读出来后单独留一份原始值（表单里统一显示成 0.0.0.0）。
    QString storedListenAddress;
};

// 这里只暴露 add / delete：netsh 的 set 改不了「侦听地址/端口」这个主键，
// 而且有只写配置不重新绑定监听的实例，编辑统一交给上层「删旧 + 加新」实现（见 portproxy.cpp 顶部说明）。
enum class PortProxyAction
{
    Add,
    Delete
};

// netsh 执行结果。started=false 表示进程没起来（如找不到 netsh.exe），output 里是原因。
struct PortProxyResult
{
    bool started = false;
    int exitCode = -1;
    QString output;
};

// 读取当前 v4tov4 转发列表（netsh interface portproxy show v4tov4），按侦听地址、端口排序。
// 失败返回 false 并回填 error。
bool queryPortProxies(QVector<PortProxyEntry> *entries, QString *error);

// 拼出完整的 netsh 参数（含 "interface portproxy" 前缀），可直接用于执行或展示。
QStringList portProxyArguments(PortProxyAction action, const PortProxyEntry &entry);

// 执行一条完整的 netsh 参数。命令必须传绝对路径的 netsh.exe，避免同目录同名 exe 劫持提权进程。
PortProxyResult runPortProxyCommand(const QStringList &arguments);

// 两条规则是否指向同一个侦听端点（即 netsh 里的同一条表项）。
bool samePortProxyEndpoint(const PortProxyEntry &a, const PortProxyEntry &b);

// IP Helper 服务（iphlpsvc）：端口转发靠它真正去监听，服务没跑时规则存在也不生效。
enum class IpHelperState
{
    Running,
    Stopped,
    Unknown
};
IpHelperState ipHelperState();

// 拼成「侦听地址:端口 → 连接地址:端口」，用于提示框和状态栏。
QString portProxyText(const PortProxyEntry &entry);

#endif // PORTPROXY_H
