#ifndef ROUTEQUERY_H
#define ROUTEQUERY_H

#include <QString>
#include <QVector>

// 一条 IPv4 路由（对应原版 `route print` 输出里的一行）。
struct RouteEntry
{
    QString destination;        // 目标网络，如 192.168.0.0
    QString mask;               // 子网掩码
    QString gateway;            // 网关；0.0.0.0 表示直连（route print 显示 On-link）
    QString interfaceName;      // 网卡名称，如「以太网」
    QString interfaceAddress;   // 网卡 IPv4 地址
    quint32 metric = 0;         // 跃点数
    quint32 protocol = 0;       // NL_ROUTE_PROTOCOL，用于显示路由来源
    bool persistent = false;    // 是否为长效路由（route add -p 写入注册表的那种）
};

// 本机 IPv4 路由表（GetIpForwardTable2）。失败返回 false 并回填 error。
bool queryIpv4Routes(QVector<RouteEntry> *routes, QString *error);

// 路由表中是否存在目标网络为 destination 的路由；mask 非空时一并比对掩码。
bool routeExists(const QString &destination, const QString &mask = QString());

#endif // ROUTEQUERY_H
