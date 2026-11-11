#include "mainwindow.h"

#include <QApplication>
#include <QCursor>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QProcess>
#include <QTimer>
#include <QMessageBox>
#include "configform.h"
#include "elevation.h"
#include "lockscreen.h"
#include "routemanager.h"
#include "sandboxmenu.h"

// 打开「系统路由管理」：当前进程已是管理员就直接开窗；否则用 runas 拉起一个新进程，
// 由新进程的 RouteManager 分支显示窗口（对齐原版「管理员运行」+「参数处理」的做法）。
static void openRouteManager()
{
    if (isProcessElevated()) {
        RouteManagerForm form;
        form.exec();
        return;
    }

    QString error;
    if (!runAsAdmin(QStringLiteral("RouteManager"), &error) && !error.isEmpty()) {
        QMessageBox::warning(nullptr, QStringLiteral("系统路由管理"),
                             QStringLiteral("以管理员身份启动失败：%1").arg(error));
    }
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // 神隐锁屏子进程：以命令行参数 superLock 独立启动，解锁后退出进程。
    // 锁屏相关的键盘钩子/焦点/鼠标锁定等状态随进程退出自动清理，避免常驻对象残留问题。
    // 对齐原版：父进程的太极托盘图标保持不变，子进程在托盘区另挂一个「锁」图标。
    if (app.arguments().contains(QStringLiteral("superLock"))) {
        QApplication::setQuitOnLastWindowClosed(false);

        // 未设置密码时提示先设置密码（防御：正常路径下父进程已校验）。
        if (lockScreenPassword().isEmpty()) {
            QMessageBox::information(nullptr, QStringLiteral("神隐锁屏设置"),
                                     QStringLiteral("尚未设置解锁密码！"));
            return 0;
        }

        LockScreen lockScreen;
        lockScreen.setPassword(lockScreenPassword());

        // 锁屏子进程自己的托盘图标（锁图标）+ 只读提示菜单。
        QMenu lockTrayMenu;
        QAction *lockedHint = new QAction(QStringLiteral("屏幕已锁定"), &lockTrayMenu);
        lockedHint->setEnabled(false);
        lockTrayMenu.addAction(lockedHint);

        QSystemTrayIcon lockTrayIcon;
        lockTrayIcon.setIcon(QIcon(QStringLiteral(":/icon/icons/lock.png")));
        lockTrayIcon.setToolTip(QStringLiteral("屏幕已锁定"));
        lockTrayIcon.setContextMenu(&lockTrayMenu);
        lockTrayIcon.show();

        QObject::connect(&lockScreen, &LockScreen::unlocked, &app, &QApplication::quit);
        lockScreen.showLockScreen();
        return app.exec();
    }

    // 系统路由管理子进程：主进程没有管理员权限时，用 runas 重新拉起本程序，
    // 新进程只显示路由管理窗口、不建托盘，关闭窗口即退出（对齐原版「管理员运行 + 参数处理」）。
    if (app.arguments().contains(QStringLiteral("RouteManager"))) {
        if (!isProcessElevated()) {
            QMessageBox::warning(nullptr, QStringLiteral("系统路由管理"),
                                 QStringLiteral("未获得管理员权限，无法管理系统路由表。"));
            return 0;
        }

        QApplication::setQuitOnLastWindowClosed(true);
        RouteManagerForm routeManager;
        routeManager.exec();
        return 0;
    }

    QApplication::setQuitOnLastWindowClosed(false);
    ensureLockScreenConfig();   // 首次启动时创建「无密码」的 ini 文件

    // 栈对象：生命周期覆盖整个 exec()，退出时自动释放，不会泄漏
    QMenu trayMenu;

    QAction *showConfigAction = new QAction("设置", &trayMenu);
    QObject::connect(showConfigAction, &QAction::triggered, []() {
        ConfigForm configForm;
        configForm.exec();
    });

    // 「系统」子菜单：系统级工具都收在这里，目前只有系统路由管理
    QMenu *systemMenu = new QMenu("系统", &trayMenu);
    QAction *routeManagerAction = new QAction("路由管理", systemMenu);
    QObject::connect(routeManagerAction, &QAction::triggered, []() {
        openRouteManager();
    });
    systemMenu->addAction(routeManagerAction);

    // 「沙箱」一级菜单：紧跟在「系统」下方，内容来自 C:\Windows\Sandboxie.ini 的 RunCommand。
    // 构造后立刻解析一次，文件不存在或没有可启动项时菜单自己隐藏，这里不用再判断。
    SandboxMenu *sandboxMenu = new SandboxMenu(&trayMenu);
    sandboxMenu->refresh();

    // 每次弹出托盘菜单前重新解析 ini：新增/删除沙箱、改 RunCommand 都会立刻反映到菜单里，
    // 也避免菜单因「没有可启动项」隐藏后，再也点不到「刷新沙箱列表」的死角。
    QObject::connect(&trayMenu, &QMenu::aboutToShow, sandboxMenu, &SandboxMenu::refresh);

    QAction *lockAction = new QAction("锁定", &trayMenu);
    QObject::connect(lockAction, &QAction::triggered, []() {
        QProcess::startDetached("rundll32.exe", QStringList() << "user32.dll,LockWorkStation");
    });

    QAction *lockScreenAction = new QAction("遮屏", &trayMenu);
    QObject::connect(lockScreenAction, &QAction::triggered, []() {
        // 未设置密码时提示先设置密码，并直接打开设置界面便于用户设置。
        if (lockScreenPassword().isEmpty()) {
            QMessageBox::information(nullptr, QStringLiteral("神隐锁屏设置"),
                                     QStringLiteral("尚未设置解锁密码！请先设置密码。"));
            ConfigForm configForm;
            configForm.exec();
            return;
        }
        // 启动锁屏子进程：父进程太极图标不变，锁屏子进程自带锁图标托盘。
        QProcess::startDetached(QApplication::applicationFilePath(),
                                QStringList() << QStringLiteral("superLock"));
    });

    QAction *quitAction = new QAction("退出", &trayMenu);
    QObject::connect(quitAction, &QAction::triggered, &app, &QApplication::quit);

    trayMenu.addAction(showConfigAction);
    trayMenu.addSeparator();
    trayMenu.addMenu(systemMenu);
    trayMenu.addMenu(sandboxMenu);
    trayMenu.addSeparator();
    trayMenu.addAction(lockAction);
    trayMenu.addAction(lockScreenAction);
    trayMenu.addSeparator();
    trayMenu.addAction(quitAction);

    QList<QIcon> frames;
    for (int i = 30; i >= 1; --i) {
        frames << QIcon(QString(":/trayIcon/icons/TaiChi1/%1.png").arg(i));
    }
    QSystemTrayIcon trayIcon;
    trayIcon.setIcon(frames[0]);
    trayIcon.setToolTip("BG Assistant");

    int currentFrame = 0;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&]() {
        currentFrame = (currentFrame + 1) % frames.size();
        trayIcon.setIcon(frames[currentFrame]);
    });
    timer.start(40);

    trayIcon.setContextMenu(&trayMenu);

    QObject::connect(&trayIcon, &QSystemTrayIcon::activated, [&](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger) {
            trayMenu.popup(QCursor::pos());
        }
    });

    trayIcon.show();

    MainWindow mainWindow;
    return app.exec();
}
