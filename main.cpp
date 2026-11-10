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

    if (app.arguments().contains("/lockscreen")) {
        LockScreen lockScreen;
        lockScreen.setPassword("a");
        lockScreen.showLockScreen();
        return app.exec();
    }

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

    QAction *lockScreenAction = new QAction("遮屏", &trayMenu);
    QObject::connect(lockScreenAction, &QAction::triggered, []() {
        QProcess::startDetached(QApplication::applicationFilePath(), QStringList() << "/lockscreen");
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

    QObject::connect(&trayIcon, &QSystemTrayIcon::activated, [&](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger) {
            trayMenu.popup(QCursor::pos());
        }
    });

    trayIcon.show();

    MainWindow mainWindow;
    return app.exec();
}
