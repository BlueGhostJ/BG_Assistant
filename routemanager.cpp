#include "routemanager.h"
#include "ui_routemanager.h"

#include "ipaddressedit.h"
#include "routequery.h"
#include "routetabledialog.h"

#include <QIcon>
#include <QMessageBox>
#include <QProcess>
#include <QStringList>

#ifndef CREATE_NO_WINDOW
#  define CREATE_NO_WINDOW 0x08000000
#endif

namespace {

// 消息框标题统一用窗口名，对齐原版（原版各提示框标题就是「系统路由管理」）
QString messageTitle()
{
    return QStringLiteral("系统路由管理");
}

struct CommandResult
{
    bool finished = false;
    int exitCode = -1;
    QString output;
};

// 直接调用 route.exe（原版是 cmd.exe /c route ...），并隐藏控制台窗口，避免黑框一闪。
CommandResult runRoute(const QStringList &arguments)
{
    CommandResult result;

    QProcess process;
    process.setProgram(QStringLiteral("route.exe"));
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

    result.finished = true;
    result.exitCode = process.exitCode();
    // route.exe 的中文输出是本地编码（GBK），不能按 UTF-8 解
    result.output = QString::fromLocal8Bit(process.readAllStandardOutput()
                                           + process.readAllStandardError())
                        .trimmed();
    return result;
}

// 按网段的第一段推导默认掩码：原版在网段变化时会自动填掩码，这里按 A/B/C 类补全；
// 0.0.0.0 是「配外网网关」的约定写法，掩码同样给 0.0.0.0。
QString defaultMaskFor(const QString &segment)
{
    bool ok = false;
    const int first = segment.section(QLatin1Char('.'), 0, 0).toInt(&ok);
    if (!ok)
        return QString();

    if (first == 0)
        return QStringLiteral("0.0.0.0");
    if (first < 128)
        return QStringLiteral("255.0.0.0");
    if (first < 192)
        return QStringLiteral("255.255.0.0");
    if (first < 224)
        return QStringLiteral("255.255.255.0");
    return QStringLiteral("255.255.255.255");
}

} // namespace

RouteManagerForm::RouteManagerForm(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::RouteManagerForm)
{
    ui->setupUi(this);
    setWindowIcon(QIcon(QStringLiteral(":/icon/icons/app.ico")));
    setAttribute(Qt::WA_QuitOnClose, false);

    // 窗口拉宽时让标签列吃掉多余宽度，输入框保持原宽度（与截图里的布局一致）
    ui->contentLayout->setColumnStretch(0, 1);
    ui->contentLayout->setColumnStretch(1, 0);

    // 关掉 autoDefault：否则在输入框里按回车会误触发「添加路由」
    ui->addButton->setAutoDefault(false);
    ui->deleteButton->setAutoDefault(false);
    ui->showTableButton->setAutoDefault(false);

    // 原版初始值：网段、掩码为 0.0.0.0，网关留空（空控件的表现就是只剩三个点）
    ui->segmentEdit->setAddress(QStringLiteral("0.0.0.0"));
    ui->maskEdit->setAddress(QStringLiteral("0.0.0.0"));
    ui->gatewayEdit->clear();

    connect(ui->addButton, &QPushButton::clicked, this, &RouteManagerForm::addRoute);
    connect(ui->deleteButton, &QPushButton::clicked, this, &RouteManagerForm::deleteRoute);
    connect(ui->showTableButton, &QPushButton::clicked, this, &RouteManagerForm::showRouteTable);

    // 网段变化时跟着推导掩码；用户手动改过掩码就不再覆盖他的输入
    connect(ui->segmentEdit, &IpAddressEdit::addressChanged, this, &RouteManagerForm::applyDefaultMask);
    connect(ui->maskEdit, &IpAddressEdit::addressChanged, this, [this]() {
        if (!m_updatingMask)
            m_maskAutoFilled = false;
    });
}

RouteManagerForm::~RouteManagerForm()
{
    delete ui;
}

void RouteManagerForm::applyDefaultMask()
{
    if (!m_maskAutoFilled)
        return;

    const QString segment = ui->segmentEdit->address();
    if (segment.isEmpty())
        return;   // 还没填完整，先不动掩码

    const QString mask = defaultMaskFor(segment);
    if (mask.isEmpty() || mask == ui->maskEdit->address())
        return;

    m_updatingMask = true;
    ui->maskEdit->setAddress(mask);
    m_updatingMask = false;
}

void RouteManagerForm::addRoute()
{
    const QString segment = ui->segmentEdit->address();
    if (segment.isEmpty()) {
        QMessageBox::warning(this, messageTitle(), QStringLiteral("请填写需要配置的网段。"));
        ui->segmentEdit->setFocus();
        return;
    }

    const QString mask = ui->maskEdit->address();
    if (mask.isEmpty()) {
        QMessageBox::warning(this, messageTitle(), QStringLiteral("请填写该网段的子网掩码。"));
        ui->maskEdit->setFocus();
        return;
    }

    const QString gateway = ui->gatewayEdit->address();
    if (gateway.isEmpty()) {
        QMessageBox::warning(this, messageTitle(), QStringLiteral("请填写该网段要使用的网关。"));
        ui->gatewayEdit->setFocus();
        return;
    }

    // route [-p] add destination mask netmask gateway metric 1
    // 原版把 -p 写在 add 后面（route add -p ...），文档语法是放在子命令前，这里用文档写法，语义一致。
    QStringList arguments;
    if (ui->persistentCheckBox->isChecked())
        arguments << QStringLiteral("-p");
    arguments << QStringLiteral("add") << segment << QStringLiteral("mask") << mask << gateway
              << QStringLiteral("metric") << QStringLiteral("1");

    const CommandResult result = runRoute(arguments);
    if (!result.finished || result.exitCode != 0) {
        QMessageBox::warning(this, messageTitle(),
                             QStringLiteral("添加路由失败。\n\n%1")
                                 .arg(result.output.isEmpty()
                                          ? QStringLiteral("请确认当前进程已获得管理员权限。")
                                          : result.output));
        return;
    }

    // 原版是 route print 到临时文件再查文本，这里直接查系统路由表
    if (routeExists(segment, mask)) {
        QMessageBox::information(this, messageTitle(),
                                 QStringLiteral("路由添加成功。\n%1 / %2 → %3")
                                     .arg(segment, mask, gateway));
    } else {
        QMessageBox::warning(this, messageTitle(),
                             QStringLiteral("命令已执行，但系统路由表中没有找到该路由，"
                                            "请用「显示系统路由表」确认。"));
    }
}

void RouteManagerForm::deleteRoute()
{
    const QString segment = ui->segmentEdit->address();
    if (segment.isEmpty()) {
        QMessageBox::warning(this, messageTitle(), QStringLiteral("请填写需要删除的网段。"));
        ui->segmentEdit->setFocus();
        return;
    }

    // 原版先查 route print，找不到就提示「系统路由表不存在此路由信息」，不执行删除
    if (!routeExists(segment)) {
        QMessageBox::information(this, messageTitle(),
                                 QStringLiteral("系统路由表中不存在该网段的路由信息。"));
        return;
    }

    const CommandResult result = runRoute(QStringList() << QStringLiteral("delete") << segment);
    if (!result.finished || result.exitCode != 0) {
        QMessageBox::warning(this, messageTitle(),
                             QStringLiteral("删除路由失败。\n\n%1")
                                 .arg(result.output.isEmpty()
                                          ? QStringLiteral("请确认当前进程已获得管理员权限。")
                                          : result.output));
        return;
    }

    if (routeExists(segment)) {
        QMessageBox::warning(this, messageTitle(),
                             QStringLiteral("删除失败：系统路由表中仍存在该网段的路由。"));
    } else {
        QMessageBox::information(this, messageTitle(),
                                 QStringLiteral("已删除 %1 的路由。").arg(segment));
    }
}

void RouteManagerForm::showRouteTable()
{
    RouteTableDialog dialog(this);
    dialog.exec();
}
