#include "routetabledialog.h"
#include "routequery.h"

#include <QAbstractItemView>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace {

enum Column
{
    ColumnDestination = 0,
    ColumnMask,
    ColumnGateway,
    ColumnInterface,
    ColumnMetric,
    ColumnType,
    ColumnPersistent,
    ColumnCount
};

// NL_ROUTE_PROTOCOL（MIB_IPPROTO_*）。这里直接写数值，避免不同 SDK 里宏名不一致导致编译失败。
QString protocolText(quint32 protocol)
{
    switch (protocol) {
    case 2:
        return QStringLiteral("本地");        // MIB_IPPROTO_LOCAL：本机地址/广播
    case 3:
        return QStringLiteral("手动");        // MIB_IPPROTO_NETMGMT：route add 或 DHCP 下发
    case 4:
        return QStringLiteral("ICMP 重定向");  // MIB_IPPROTO_ICMP
    case 10002:
        return QStringLiteral("自动");        // MIB_IPPROTO_NT_AUTOSTATIC
    case 10006:
    case 10007:
        return QStringLiteral("静态");        // MIB_IPPROTO_NT_STATIC(_NON_DOD)
    default:
        return QStringLiteral("系统");
    }
}

} // namespace

RouteTableDialog::RouteTableDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("系统路由表"));
    setWindowIcon(QIcon(QStringLiteral(":/icon/icons/app.ico")));
    setAttribute(Qt::WA_QuitOnClose, false);
    setSizeGripEnabled(true);
    resize(860, 460);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    m_summary = new QLabel(this);
    layout->addWidget(m_summary);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(ColumnCount);
    m_table->setHorizontalHeaderLabels({QStringLiteral("目标网络"),
                                        QStringLiteral("子网掩码"),
                                        QStringLiteral("网关"),
                                        QStringLiteral("接口"),
                                        QStringLiteral("跃点数"),
                                        QStringLiteral("类型"),
                                        QStringLiteral("长效")});
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setAlternatingRowColors(true);
    m_table->setWordWrap(false);
    m_table->setTextElideMode(Qt::ElideRight);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(24);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    layout->addWidget(m_table, 1);

    // 列宽按内容量分配，「接口」吃掉剩余宽度（网卡名 + IP 最长）
    m_table->setColumnWidth(ColumnDestination, 140);
    m_table->setColumnWidth(ColumnMask, 140);
    m_table->setColumnWidth(ColumnGateway, 130);
    m_table->setColumnWidth(ColumnMetric, 70);
    m_table->setColumnWidth(ColumnType, 100);
    m_table->setColumnWidth(ColumnPersistent, 60);
    m_table->horizontalHeader()->setSectionResizeMode(ColumnInterface, QHeaderView::Stretch);

    auto *buttonLayout = new QHBoxLayout;
    buttonLayout->addStretch();
    auto *refreshButton = new QPushButton(QStringLiteral("刷新"), this);
    auto *closeButton = new QPushButton(QStringLiteral("关闭"), this);
    refreshButton->setMinimumWidth(80);
    closeButton->setMinimumWidth(80);
    buttonLayout->addWidget(refreshButton);
    buttonLayout->addWidget(closeButton);
    layout->addLayout(buttonLayout);

    connect(refreshButton, &QPushButton::clicked, this, &RouteTableDialog::refresh);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);

    refresh();
}

void RouteTableDialog::refresh()
{
    QVector<RouteEntry> routes;
    QString error;
    if (!queryIpv4Routes(&routes, &error)) {
        m_table->setRowCount(0);
        m_summary->setText(error);
        return;
    }

    const int count = int(routes.size());

    m_table->setUpdatesEnabled(false);
    m_table->setRowCount(count);

    for (int row = 0; row < count; ++row) {
        const RouteEntry &entry = routes.at(row);

        QString interfaceText = entry.interfaceName;
        if (!entry.interfaceAddress.isEmpty()) {
            interfaceText = interfaceText.isEmpty()
                                ? entry.interfaceAddress
                                : QStringLiteral("%1 (%2)").arg(interfaceText, entry.interfaceAddress);
        }

        const QStringList values{
            entry.destination,
            entry.mask,
            entry.gateway == QLatin1String("0.0.0.0") ? QStringLiteral("在链路上") : entry.gateway,
            interfaceText,
            QString::number(entry.metric),
            protocolText(entry.protocol),
            entry.persistent ? QStringLiteral("是") : QString(),
        };

        for (int column = 0; column < ColumnCount; ++column) {
            auto *item = new QTableWidgetItem(values.at(column));
            if (column == ColumnMetric)
                item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_table->setItem(row, column, item);
        }
    }

    m_table->setUpdatesEnabled(true);
    m_summary->setText(QStringLiteral("共 %1 条 IPv4 路由；网关显示「在链路上」表示该网段为直连。")
                           .arg(count));
}
