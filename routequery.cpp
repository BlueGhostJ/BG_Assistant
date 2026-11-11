// 本文件直接用 Windows IP Helper API 读路由表：
// 原版是 `route print` 重定向到临时文件再用 notepad++ 打开，这里换成结构化查询，既不用临时文件，
// 也能拿到网卡名称、跃点数、路由来源等 `route print` 里要肉眼解析的信息。
#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef _WIN32_WINNT
#    define _WIN32_WINNT 0x0600   // Vista+：GetIpForwardTable2 / GetAdaptersAddresses
#  endif
#  include <winsock2.h>           // 必须先于 windows.h，否则会拉进冲突的 winsock1 定义
#  include <ws2tcpip.h>
#  include <windows.h>
#  include <iphlpapi.h>
#  include <netioapi.h>
#endif

#include "routequery.h"

#include <QByteArray>
#include <QHash>
#include <QSet>
#include <QSettings>
#include <QStringList>

#include <algorithm>

#ifdef _WIN32
namespace {

struct InterfaceInfo
{
    QString name;
    QString address;
};

QString ipv4Text(const SOCKADDR_INET &address)
{
    if (address.si_family != AF_INET)
        return QString();

    const unsigned char *bytes = reinterpret_cast<const unsigned char *>(&address.Ipv4.sin_addr);
    return QStringLiteral("%1.%2.%3.%4")
        .arg(int(bytes[0]))
        .arg(int(bytes[1]))
        .arg(int(bytes[2]))
        .arg(int(bytes[3]));
}

// 前缀长度 -> 点分掩码（/0 与 /32 单独处理，避免移位越界）
QString maskText(ULONG prefixLength)
{
    quint32 mask = 0;
    if (prefixLength >= 32)
        mask = 0xFFFFFFFFu;
    else if (prefixLength > 0)
        mask = 0xFFFFFFFFu << (32 - prefixLength);

    return QStringLiteral("%1.%2.%3.%4")
        .arg((mask >> 24) & 0xFFu)
        .arg((mask >> 16) & 0xFFu)
        .arg((mask >> 8) & 0xFFu)
        .arg(mask & 0xFFu);
}

// 接口索引 -> 网卡名称/IP，供路由行显示「数据从哪块网卡出去」
QHash<quint32, InterfaceInfo> queryInterfaces()
{
    QHash<quint32, InterfaceInfo> infos;

    ULONG size = 16 * 1024;
    QByteArray buffer(int(size), Qt::Uninitialized);
    auto *addresses = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buffer.data());
    const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;

    ULONG result = GetAdaptersAddresses(AF_INET, flags, nullptr, addresses, &size);
    if (result == ERROR_BUFFER_OVERFLOW) {
        buffer.resize(int(size));
        addresses = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buffer.data());
        result = GetAdaptersAddresses(AF_INET, flags, nullptr, addresses, &size);
    }
    if (result != NO_ERROR)
        return infos;

    for (IP_ADAPTER_ADDRESSES *item = addresses; item; item = item->Next) {
        InterfaceInfo info;
        if (item->FriendlyName)
            info.name = QString::fromWCharArray(item->FriendlyName);

        if (item->FirstUnicastAddress) {
            const sockaddr *address = item->FirstUnicastAddress->Address.lpSockaddr;
            if (address && address->sa_family == AF_INET) {
                const auto *ipv4 = reinterpret_cast<const sockaddr_in *>(address);
                const unsigned char *bytes = reinterpret_cast<const unsigned char *>(&ipv4->sin_addr);
                info.address = QStringLiteral("%1.%2.%3.%4")
                                   .arg(int(bytes[0]))
                                   .arg(int(bytes[1]))
                                   .arg(int(bytes[2]))
                                   .arg(int(bytes[3]));
            }
        }

        infos.insert(quint32(item->IfIndex), info);
    }

    return infos;
}

// 长效路由（route add -p）存放在这个注册表键下，值名形如 "192.168.5.0,255.255.255.0,192.168.1.1,-1"
QSet<QString> queryPersistentRoutes()
{
    QSet<QString> routes;
    QSettings registry(
        QStringLiteral("HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\\PersistentRoutes"),
        QSettings::NativeFormat);

    const QStringList names = registry.childKeys();
    for (const QString &name : names) {
        const QStringList fields = name.split(QLatin1Char(','), Qt::SkipEmptyParts);
        if (fields.size() < 3)
            continue;
        routes.insert(QStringLiteral("%1|%2|%3")
                          .arg(fields.at(0).trimmed(), fields.at(1).trimmed(), fields.at(2).trimmed()));
    }
    return routes;
}

quint32 ipv4Key(const QString &ip)
{
    const QStringList parts = ip.split(QLatin1Char('.'));
    if (parts.size() != 4)
        return 0;

    return (parts.at(0).toUInt() << 24) | (parts.at(1).toUInt() << 16)
           | (parts.at(2).toUInt() << 8) | parts.at(3).toUInt();
}

} // namespace
#endif // _WIN32

bool queryIpv4Routes(QVector<RouteEntry> *routes, QString *error)
{
    if (!routes)
        return false;

    routes->clear();

#ifdef _WIN32
    PMIB_IPFORWARD_TABLE2 table = nullptr;
    const NETIO_STATUS status = GetIpForwardTable2(AF_INET, &table);
    if (status != NO_ERROR || !table) {
        if (error)
            *error = QStringLiteral("读取系统路由表失败（错误码 %1）。").arg(quint32(status));
        return false;
    }

    const QHash<quint32, InterfaceInfo> interfaces = queryInterfaces();
    const QSet<QString> persistentRoutes = queryPersistentRoutes();

    for (ULONG i = 0; i < table->NumEntries; ++i) {
        const MIB_IPFORWARD_ROW2 &row = table->Table[i];

        RouteEntry entry;
        entry.destination = ipv4Text(row.DestinationPrefix.Prefix);
        if (entry.destination.isEmpty())
            continue;   // 只要 IPv4（请求的 AF_INET 已经过滤，这里再兜一次底）

        entry.mask = maskText(row.DestinationPrefix.PrefixLength);
        entry.gateway = ipv4Text(row.NextHop);

        const InterfaceInfo info = interfaces.value(quint32(row.InterfaceIndex));
        entry.interfaceName = info.name;
        entry.interfaceAddress = info.address;

        entry.metric = quint32(row.Metric);
        entry.protocol = quint32(row.Protocol);
        entry.persistent = persistentRoutes.contains(
            QStringLiteral("%1|%2|%3").arg(entry.destination, entry.mask, entry.gateway));

        routes->append(entry);
    }

    FreeMibTable(table);

    // 与 `route print` 的观感保持一致：按目标网络升序，同网段再按跃点数排
    std::sort(routes->begin(), routes->end(), [](const RouteEntry &a, const RouteEntry &b) {
        const quint32 keyA = ipv4Key(a.destination);
        const quint32 keyB = ipv4Key(b.destination);
        if (keyA != keyB)
            return keyA < keyB;
        if (a.metric != b.metric)
            return a.metric < b.metric;
        return a.gateway < b.gateway;
    });

    return true;
#else
    if (error)
        *error = QStringLiteral("系统路由表仅支持 Windows。");
    return false;
#endif
}

bool routeExists(const QString &destination, const QString &mask)
{
    QVector<RouteEntry> routes;
    if (!queryIpv4Routes(&routes, nullptr))
        return false;

    for (const RouteEntry &entry : routes) {
        if (entry.destination == destination && (mask.isEmpty() || entry.mask == mask))
            return true;
    }
    return false;
}
