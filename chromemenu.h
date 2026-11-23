#ifndef CHROMEMENU_H
#define CHROMEMENU_H

#include <QList>
#include <QMenu>

#include "chromemultiopen.h"

// 托盘「系统 › Chrome 多开」二级菜单：
//
//     <多开名称> ……
//     ──────────
//     多开管理…
//
// 点名称直接用对应的数据目录和参数启动 chrome.exe（普通权限，不提权）；
// 点「多开管理…」会以 ChromeMultiOpen 参数另起一个本程序进程来显示管理窗口，
// 主程序的托盘图标不受影响（与路由管理同一套「新进程 + 参数」的做法，只是不跑 runas）。
//
// 内容在每次弹出托盘菜单前由 main.cpp 接到 aboutToShow 的 refresh() 重新读取，
// 所以在管理窗口里新建/改名后，下次弹出菜单就是最新的。
// 本机找不到 chrome.exe 时整个菜单隐藏。
class ChromeMenu : public QMenu
{
    Q_OBJECT

public:
    explicit ChromeMenu(QWidget *parent = nullptr);

    void refresh();

private:
    QList<ChromeProfile> m_profiles;

    void launchProfile(const ChromeProfile &profile);
    void openManager();
};

#endif // CHROMEMENU_H
