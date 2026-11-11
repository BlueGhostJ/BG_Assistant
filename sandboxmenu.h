#ifndef SANDBOXMENU_H
#define SANDBOXMENU_H

#include <QList>
#include <QMenu>

#include "sandboxie.h"

// 托盘一级菜单「沙箱」：解析 Sandboxie.ini 里每个沙箱的 RunCommand，
// 点菜单项就用 SandMan.exe /box:<沙箱名> <映射后的程序> 启动对应沙箱。
//
// 菜单结构（固定顺序）：
//     显示启动命令
//     ──────────
//     <沙箱名> ……
//
// 内容在每次弹出托盘菜单前由 main.cpp 接到 aboutToShow 的 refresh() 重新解析，
// 所以不再单独提供「刷新」项。
// 配置文件不存在、或没有任何带 RunCommand 的沙箱时，整个菜单隐藏（连一级菜单都不出现）。
class SandboxMenu : public QMenu
{
    Q_OBJECT

public:
    explicit SandboxMenu(QWidget *parent = nullptr);

    void refresh();

private:
    QList<SandboxEntry> m_entries;

    void showCommands() const;
    void launchEntry(const SandboxEntry &entry);
};

#endif // SANDBOXMENU_H
