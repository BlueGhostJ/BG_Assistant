#ifndef ROUTEMANAGER_H
#define ROUTEMANAGER_H

#include <QDialog>

QT_BEGIN_NAMESPACE
namespace Ui {
class RouteManagerForm;
}
QT_END_NAMESPACE

// 系统路由管理（对应原易语言版的「系统路由管理」窗口）：
// 按网段/掩码/网关添加路由，按网段删除路由，并可查看系统路由表。
// 需要管理员权限，由 main.cpp 以 RouteManager 命令行参数提权启动子进程后显示。
class RouteManagerForm : public QDialog
{
    Q_OBJECT

public:
    explicit RouteManagerForm(QWidget *parent = nullptr);
    ~RouteManagerForm() override;

private slots:
    void addRoute();
    void deleteRoute();
    void showRouteTable();

private:
    Ui::RouteManagerForm *ui;

    bool m_maskAutoFilled = true;   // 掩码仍是按网段自动推导出来的（用户没手动改过）
    bool m_updatingMask = false;

    void applyDefaultMask();
};

#endif // ROUTEMANAGER_H
