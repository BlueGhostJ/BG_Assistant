#include "chromemenu.h"

#include <QAction>
#include <QCoreApplication>
#include <QMessageBox>
#include <QProcess>
#include <QStringList>

ChromeMenu::ChromeMenu(QWidget *parent)
    : QMenu(QStringLiteral("Chrome多开"), parent)
{
}

void ChromeMenu::refresh()
{
    // 菜单项全是动态的，整体重建最省事
    clear();

    // 多开完全依赖本机的 chrome.exe；找不到时连菜单一起隐藏，
    // 避免留一个点了必然失败、也不知道该修哪的入口。
    if (ChromeMultiOpen::chromePath().isEmpty()) {
        m_profiles.clear();
        menuAction()->setVisible(false);
        return;
    }

    m_profiles = ChromeMultiOpen::loadProfiles();

    for (const ChromeProfile &profile : m_profiles) {
        QAction *action = addAction(profile.name);
        connect(action, &QAction::triggered, this,
                [this, profile]() { launchProfile(profile); });
    }

    if (!m_profiles.isEmpty())
        addSeparator();

    QAction *manageAction = addAction(QStringLiteral("多开管理…"));
    connect(manageAction, &QAction::triggered, this, &ChromeMenu::openManager);

    menuAction()->setVisible(true);
}

void ChromeMenu::launchProfile(const ChromeProfile &profile)
{
    QString error;
    if (!ChromeMultiOpen::launch(profile, &error))
        QMessageBox::warning(nullptr, QStringLiteral("Chrome多开"), error);
}

void ChromeMenu::openManager()
{
    // 新进程 + 参数：main.cpp 里 ChromeMultiOpen 分支只显示管理窗口，不建托盘。
    // 这里不需要管理员权限，所以不像路由/端口转发那样走 runas。
    if (!QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                 QStringList() << QStringLiteral("ChromeMultiOpen"))) {
        QMessageBox::warning(nullptr, QStringLiteral("Chrome多开"),
                             QStringLiteral("无法启动多开管理窗口。"));
    }
}
