#include "mainwindow.h"

#include <QApplication>
#include <QCursor>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QProcess>
#include <QTimer>
#include "configform.h"
#include "lockscreen.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // 神隐锁屏子进程：以命令行参数 superLock 独立启动，解锁后退出进程。
    // 锁屏相关的键盘钩子/焦点/鼠标锁定等状态随进程退出自动清理，避免常驻对象残留问题。
    if (app.arguments().contains(QStringLiteral("superLock"))) {
        LockScreen lockScreen;
        lockScreen.setPassword(lockScreenPassword());
        QObject::connect(&lockScreen, &LockScreen::unlocked, &app, &QApplication::quit);
        lockScreen.showLockScreen();
        return app.exec();
    }

    QApplication::setQuitOnLastWindowClosed(false);
    ensureLockScreenConfig();   // 首次启动时自动创建 ini 并写入默认密码

    // 栈对象：生命周期覆盖整个 exec()，退出时自动释放，不会泄漏
    QMenu trayMenu;

    QAction *showConfigAction = new QAction("设置", &trayMenu);
    QObject::connect(showConfigAction, &QAction::triggered, []() {
        ConfigForm configForm;
        configForm.exec();
    });

    QAction *lockAction = new QAction("锁定", &trayMenu);
    QObject::connect(lockAction, &QAction::triggered, []() {
        QProcess::startDetached("rundll32.exe", QStringList() << "user32.dll,LockWorkStation");
    });

    QAction *lockScreenAction = new QAction("遮屏", &trayMenu);

    QAction *quitAction = new QAction("退出", &trayMenu);
    QObject::connect(quitAction, &QAction::triggered, &app, &QApplication::quit);

    trayMenu.addAction(showConfigAction);
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

    // 锁屏以独立子进程运行：启动时托盘切「锁」图标并暂停动画，子进程退出后恢复。
    QProcess lockScreenProcess;
    QObject::connect(lockScreenAction, &QAction::triggered, [&]() {
        if (lockScreenProcess.state() != QProcess::NotRunning)
            return;
        timer.stop();
        trayIcon.setIcon(QIcon(QStringLiteral(":/icon/icons/lock.png")));
        lockScreenProcess.start(QApplication::applicationFilePath(),
                                QStringList() << QStringLiteral("superLock"));
    });
    QObject::connect(&lockScreenProcess,
                     QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                     [&](int exitCode, QProcess::ExitStatus exitStatus) {
        Q_UNUSED(exitCode)
        Q_UNUSED(exitStatus)
        trayIcon.setIcon(frames[currentFrame]);
        timer.start();
    });

    QObject::connect(&trayIcon, &QSystemTrayIcon::activated, [&](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger) {
            trayMenu.popup(QCursor::pos());
        }
    });

    trayIcon.show();

    MainWindow mainWindow;
    return app.exec();
}
