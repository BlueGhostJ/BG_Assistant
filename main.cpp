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

    QApplication::setQuitOnLastWindowClosed(false);

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

    // 神隐锁屏：在当前进程内直接弹出全屏锁屏窗口，不再走 /lockscreen 子进程。
    LockScreen lockScreen;
    lockScreen.setPassword(lockScreenPassword());

    QAction *lockScreenAction = new QAction("遮屏", &trayMenu);
    QObject::connect(lockScreenAction, &QAction::triggered, [&lockScreen]() {
        lockScreen.showLockScreen();
    });

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

    // 锁屏期间托盘切换为「锁」图标，解锁后恢复太极动画（对齐原版）。
    QObject::connect(&lockScreen, &LockScreen::lockStateChanged, [&](bool locked) {
        if (locked) {
            timer.stop();
            trayIcon.setIcon(QIcon(QStringLiteral(":/icon/icons/lock.png")));
        } else {
            trayIcon.setIcon(frames[currentFrame]);
            timer.start();
        }
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
