#include "portproxyform.h"
#include "ui_portproxyform.h"

#include "ipaddressedit.h"

#include <QAbstractItemView>
#include <QHeaderView>
#include <QIcon>
#include <QIntValidator>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>

namespace {

enum Column
{
    ColumnListenAddress = 0,
    ColumnListenPort,
    ColumnConnectAddress,
    ColumnConnectPort,
    ColumnCount
};

// 提示文字里给「失败」用的红色：浅色、深色主题下都能看清
const char *const kWarningStyle = "color: #d9534f;";

} // namespace

PortProxyForm::PortProxyForm(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::PortProxyForm)
{
    ui->setupUi(this);
    setWindowIcon(QIcon(QStringLiteral(":/icon/icons/app.ico")));
    setAttribute(Qt::WA_QuitOnClose, false);

    // 列表只读、整行选中；列宽跟着内容走，两个地址列吃掉多余宽度
    ui->ruleTable->setColumnCount(ColumnCount);
    ui->ruleTable->setHorizontalHeaderLabels({QStringLiteral("侦听地址"),
                                              QStringLiteral("侦听端口"),
                                              QStringLiteral("连接地址"),
                                              QStringLiteral("连接端口")});
    ui->ruleTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->ruleTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->ruleTable->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->ruleTable->setAlternatingRowColors(true);
    ui->ruleTable->setWordWrap(false);
    ui->ruleTable->setTextElideMode(Qt::ElideRight);
    ui->ruleTable->verticalHeader()->setVisible(false);
    ui->ruleTable->verticalHeader()->setDefaultSectionSize(24);
    ui->ruleTable->horizontalHeader()->setSectionResizeMode(ColumnListenAddress, QHeaderView::Stretch);
    ui->ruleTable->horizontalHeader()->setSectionResizeMode(ColumnListenPort, QHeaderView::ResizeToContents);
    ui->ruleTable->horizontalHeader()->setSectionResizeMode(ColumnConnectAddress, QHeaderView::Stretch);
    ui->ruleTable->horizontalHeader()->setSectionResizeMode(ColumnConnectPort, QHeaderView::ResizeToContents);

    // 端口只收 1-65535；留空的情况由按钮点击时的校验统一提示
    ui->listenPortEdit->setValidator(new QIntValidator(1, 65535, this));
    ui->connectPortEdit->setValidator(new QIntValidator(1, 65535, this));

    // 关掉 autoDefault：否则在端口输入框里按回车会误触发「添加转发」
    ui->refreshButton->setAutoDefault(false);
    ui->addButton->setAutoDefault(false);
    ui->updateButton->setAutoDefault(false);
    ui->deleteButton->setAutoDefault(false);
    ui->closeButton->setAutoDefault(false);

    connect(ui->refreshButton, &QPushButton::clicked, this, &PortProxyForm::refresh);
    connect(ui->addButton, &QPushButton::clicked, this, &PortProxyForm::addProxy);
    connect(ui->updateButton, &QPushButton::clicked, this, &PortProxyForm::updateSelected);
    connect(ui->deleteButton, &QPushButton::clicked, this, &PortProxyForm::deleteSelected);
    connect(ui->closeButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(ui->ruleTable, &QTableWidget::itemSelectionChanged,
            this, &PortProxyForm::handleSelectionChanged);

    refresh();
}

PortProxyForm::~PortProxyForm()
{
    delete ui;
}

void PortProxyForm::refresh()
{
    const PortProxyEntry keep = m_selected;   // 刷新后尽量保住原来选中的那条

    QVector<PortProxyEntry> entries;
    QString error;
    if (!queryPortProxies(&entries, &error)) {
        m_entries.clear();
        ui->ruleTable->setRowCount(0);
        m_selected = PortProxyEntry();
        updateButtons();
        updateServiceState();
        setMessage(error, true);
        return;
    }

    m_entries = entries;

    {
        const QSignalBlocker blocker(ui->ruleTable);
        ui->ruleTable->setUpdatesEnabled(false);
        ui->ruleTable->setRowCount(int(entries.size()));

        for (int row = 0; row < entries.size(); ++row) {
            const PortProxyEntry &entry = entries.at(row);
            const QStringList texts{entry.listenAddress,
                                    QString::number(int(entry.listenPort)),
                                    entry.connectAddress,
                                    QString::number(int(entry.connectPort))};

            for (int column = 0; column < ColumnCount; ++column) {
                auto *item = new QTableWidgetItem(texts.at(column));
                if (column == ColumnListenPort || column == ColumnConnectPort)
                    item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                ui->ruleTable->setItem(row, column, item);
            }
        }

        ui->ruleTable->setUpdatesEnabled(true);
    }

    // 先清掉旧选中：表格按行号保留选中状态，重建后行号可能对应到别的规则
    ui->ruleTable->clearSelection();
    if (keep.listenPort == 0 || !selectEntry(keep))
        m_selected = PortProxyEntry();

    updateButtons();
    updateServiceState();
    setMessage(entries.isEmpty() ? QStringLiteral("当前没有任何转发规则。")
                                 : QStringLiteral("共 %1 条转发规则。").arg(int(entries.size())));
}

void PortProxyForm::handleSelectionChanged()
{
    const QModelIndexList rows = ui->ruleTable->selectionModel()->selectedRows();
    if (rows.isEmpty()) {
        m_selected = PortProxyEntry();
        updateButtons();
        return;
    }

    const int row = rows.first().row();
    if (row < 0 || row >= m_entries.size()) {
        m_selected = PortProxyEntry();
        updateButtons();
        return;
    }

    m_selected = m_entries.at(row);
    fillForm(m_selected);
    updateButtons();
}

void PortProxyForm::addProxy()
{
    PortProxyEntry entry;
    if (!readForm(&entry))
        return;

    // 同一个侦听端点只能有一条规则，重复添加会被 netsh 当成覆盖，这里先挡掉
    if (containsEntry(entry)) {
        QMessageBox::warning(this, windowTitle(),
                             QStringLiteral("已存在侦听 %1:%2 的转发。\n"
                                            "要修改它，请在上方列表中选中该行再点「更新选中」。")
                                 .arg(entry.listenAddress)
                                 .arg(int(entry.listenPort)));
        return;
    }

    QString error;
    if (!execute(PortProxyAction::Add, entry, &error)) {
        QMessageBox::warning(this, windowTitle(),
                             QStringLiteral("添加转发失败。\n\n%1").arg(error));
        return;
    }

    m_selected = entry;
    refresh();

    if (!containsEntry(entry)) {
        QMessageBox::warning(this, windowTitle(),
                             QStringLiteral("命令已执行，但转发列表里没有找到这条规则，请点「刷新」确认。"));
        return;
    }

    setMessage(QStringLiteral("已添加转发：%1").arg(portProxyText(entry)));
}

void PortProxyForm::updateSelected()
{
    if (m_selected.listenPort == 0) {
        QMessageBox::information(this, windowTitle(),
                                 QStringLiteral("请先在上方列表中选中要修改的转发。"));
        return;
    }

    PortProxyEntry entry;
    if (!readForm(&entry))
        return;

    const PortProxyEntry original = m_selected;
    const bool listenerChanged = !samePortProxyEndpoint(original, entry);
    QString error;

    // 什么都没改就别白删一次：重建规则会有一段断流
    if (entry.listenAddress.compare(original.listenAddress, Qt::CaseInsensitive) == 0
        && entry.listenPort == original.listenPort
        && entry.connectAddress.compare(original.connectAddress, Qt::CaseInsensitive) == 0
        && entry.connectPort == original.connectPort) {
        setMessage(QStringLiteral("内容没有变化，未做修改。"));
        return;
    }

    if (listenerChanged) {
        // 换侦听端点等于换了一条表项：新端点不能和别的规则撞车
        for (const PortProxyEntry &item : m_entries) {
            if (samePortProxyEndpoint(item, original) || !samePortProxyEndpoint(item, entry))
                continue;
            QMessageBox::warning(this, windowTitle(),
                                 QStringLiteral("已存在侦听 %1:%2 的转发，请换一个侦听地址或端口。")
                                     .arg(entry.listenAddress)
                                     .arg(int(entry.listenPort)));
            return;
        }

        const auto answer = QMessageBox::question(
            this, windowTitle(),
            QStringLiteral("侦听地址或端口变了，需要重建规则：\n\n旧：%1\n新：%2\n\n"
                           "会先添加新规则、成功后再删除旧规则，期间旧转发保持可用。是否继续？")
                .arg(portProxyText(original), portProxyText(entry)),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;

        if (!execute(PortProxyAction::Add, entry, &error)) {
            QMessageBox::warning(this, windowTitle(),
                                 QStringLiteral("修改失败：新规则没有添加成功，旧规则未改动。\n\n%1")
                                     .arg(error));
            return;
        }

        m_selected = entry;
        if (!execute(PortProxyAction::Delete, original, &error)) {
            refresh();
            QMessageBox::warning(this, windowTitle(),
                                 QStringLiteral("新规则已添加，但旧规则删除失败，现在两条规则同时存在，"
                                                "请手动删除旧的那条。\n\n%1")
                                     .arg(error));
            return;
        }
    } else {
        // 侦听端点没变：同一个表项只能先删后加，中间有极短的断流
        if (!execute(PortProxyAction::Delete, original, &error)) {
            QMessageBox::warning(this, windowTitle(),
                                 QStringLiteral("修改失败：删除旧规则时出错，原规则保持不变。\n\n%1")
                                     .arg(error));
            return;
        }
        if (!execute(PortProxyAction::Add, entry, &error)) {
            m_selected = PortProxyEntry();
            refresh();
            QMessageBox::warning(this, windowTitle(),
                                 QStringLiteral("旧规则已删除，但新规则添加失败，这条转发现在不存在了。\n\n%1")
                                     .arg(error));
            return;
        }
        m_selected = entry;
    }

    refresh();

    if (!containsEntry(entry)) {
        QMessageBox::warning(this, windowTitle(),
                             QStringLiteral("命令已执行，但列表里没有找到修改后的规则，请点「刷新」确认。"));
        return;
    }

    setMessage(QStringLiteral("已更新转发：%1").arg(portProxyText(entry)));
}

void PortProxyForm::deleteSelected()
{
    if (m_selected.listenPort == 0) {
        QMessageBox::information(this, windowTitle(),
                                 QStringLiteral("请先在上方列表中选中要删除的转发。"));
        return;
    }

    const PortProxyEntry original = m_selected;
    const auto answer = QMessageBox::question(
        this, windowTitle(),
        QStringLiteral("确定删除这条转发吗？\n\n%1\n\n删除后这个端口立即停止转发。")
            .arg(portProxyText(original)),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    QString error;
    if (!execute(PortProxyAction::Delete, original, &error)) {
        QMessageBox::warning(this, windowTitle(),
                             QStringLiteral("删除转发失败。\n\n%1").arg(error));
        return;
    }

    m_selected = PortProxyEntry();
    refresh();

    if (containsEntry(original)) {
        QMessageBox::warning(this, windowTitle(),
                             QStringLiteral("命令已执行，但列表里仍能找到这条规则，请点「刷新」确认。"));
        return;
    }

    setMessage(QStringLiteral("已删除转发：%1").arg(portProxyText(original)));
}

bool PortProxyForm::readForm(PortProxyEntry *entry)
{
    const QString listenAddress = ui->listenAddressEdit->address();
    if (listenAddress.isEmpty()) {
        QMessageBox::warning(this, windowTitle(),
                             QStringLiteral("请填写侦听地址：要监听本机的哪个 IPv4 地址。\n"
                                            "填 0.0.0.0 表示所有网卡。"));
        ui->listenAddressEdit->setFocus();
        return false;
    }

    bool ok = false;
    const int listenPort = ui->listenPortEdit->text().trimmed().toInt(&ok);
    if (!ok || listenPort < 1 || listenPort > 65535) {
        QMessageBox::warning(this, windowTitle(),
                             QStringLiteral("侦听端口需要填写 1-65535 之间的整数。"));
        ui->listenPortEdit->setFocus();
        return false;
    }

    const QString connectAddress = ui->connectAddressEdit->address();
    if (connectAddress.isEmpty()) {
        QMessageBox::warning(this, windowTitle(),
                             QStringLiteral("请填写连接地址：要把流量转发到哪个 IPv4 地址。"));
        ui->connectAddressEdit->setFocus();
        return false;
    }

    const int connectPort = ui->connectPortEdit->text().trimmed().toInt(&ok);
    if (!ok || connectPort < 1 || connectPort > 65535) {
        QMessageBox::warning(this, windowTitle(),
                             QStringLiteral("连接端口需要填写 1-65535 之间的整数。"));
        ui->connectPortEdit->setFocus();
        return false;
    }

    // 回环：转出去的连接又会命中自己的侦听，只会打成死循环
    if (listenAddress.compare(connectAddress, Qt::CaseInsensitive) == 0 && listenPort == connectPort) {
        QMessageBox::warning(this, windowTitle(),
                             QStringLiteral("侦听地址、端口与连接地址、端口完全相同，连接会转回自己，请检查。"));
        return false;
    }

    // 0.0.0.0 占的是本机所有 IP：同端口再转到本机任意地址（含 127.0.0.1）一样会连回自己
    if (listenAddress == QLatin1String("0.0.0.0") && listenPort == connectPort
        && isLocalIpv4Address(connectAddress)) {
        QMessageBox::warning(
            this, windowTitle(),
            QStringLiteral("这条规则会形成回环：\n\n"
                           "侦听地址 0.0.0.0 会占用本机所有 IP 的 %1 端口；"
                           "规则又要把连接转到本机地址 %2 的同一个端口，"
                           "转出去的连接会再次命中自己的侦听，形成死循环。\n\n"
                           "请改掉其中一个端口；如果只想给某块网卡做转发，"
                           "把侦听地址填成那块网卡的 IP（如 192.168.81.1）。")
                .arg(int(listenPort))
                .arg(connectAddress));
        return false;
    }

    entry->listenAddress = listenAddress;
    entry->listenPort = quint16(listenPort);
    entry->connectAddress = connectAddress;
    entry->connectPort = quint16(connectPort);
    return true;
}

void PortProxyForm::fillForm(const PortProxyEntry &entry)
{
    ui->listenAddressEdit->setAddress(entry.listenAddress);
    ui->listenPortEdit->setText(QString::number(int(entry.listenPort)));
    ui->connectAddressEdit->setAddress(entry.connectAddress);
    ui->connectPortEdit->setText(QString::number(int(entry.connectPort)));
}

bool PortProxyForm::selectEntry(const PortProxyEntry &entry)
{
    for (int row = 0; row < m_entries.size(); ++row) {
        if (!samePortProxyEndpoint(m_entries.at(row), entry))
            continue;

        ui->ruleTable->selectRow(row);
        ui->ruleTable->scrollToItem(ui->ruleTable->item(row, ColumnListenAddress));
        return true;
    }
    return false;
}

bool PortProxyForm::containsEntry(const PortProxyEntry &entry) const
{
    for (const PortProxyEntry &item : m_entries) {
        if (samePortProxyEndpoint(item, entry))
            return true;
    }
    return false;
}

bool PortProxyForm::execute(PortProxyAction action, const PortProxyEntry &entry, QString *error)
{
    const QStringList arguments = portProxyArguments(action, entry);
    const PortProxyResult result = runPortProxyCommand(arguments);
    if (result.started && result.exitCode == 0)
        return true;

    // 失败时把 netsh 的原始输出和完整命令行一起给出来，方便手动重试或查原因
    const QString detail = result.output.isEmpty()
                               ? QStringLiteral("请确认当前进程已获得管理员权限。")
                               : result.output;
    if (error)
        *error = QStringLiteral("%1\n\n命令：netsh %2").arg(detail, arguments.join(QLatin1Char(' ')));
    return false;
}

void PortProxyForm::setMessage(const QString &text, bool warning)
{
    ui->messageLabel->setText(text);
    ui->messageLabel->setStyleSheet(warning ? QString::fromLatin1(kWarningStyle) : QString());
}

void PortProxyForm::updateButtons()
{
    const bool hasSelection = m_selected.listenPort != 0;
    ui->updateButton->setEnabled(hasSelection);
    ui->deleteButton->setEnabled(hasSelection);
}

void PortProxyForm::updateServiceState()
{
    switch (ipHelperState()) {
    case IpHelperState::Running:
        ui->serviceLabel->setText(QStringLiteral("IP Helper 服务（iphlpsvc）正在运行。"));
        ui->serviceLabel->setStyleSheet(QString());
        break;
    case IpHelperState::Stopped:
        ui->serviceLabel->setText(
            QStringLiteral("IP Helper 服务（iphlpsvc）未运行，端口转发不会生效。"));
        ui->serviceLabel->setStyleSheet(QString::fromLatin1(kWarningStyle));
        break;
    case IpHelperState::Unknown:
        ui->serviceLabel->setText(QStringLiteral("无法读取 IP Helper 服务（iphlpsvc）状态。"));
        ui->serviceLabel->setStyleSheet(QString());
        break;
    }
}
