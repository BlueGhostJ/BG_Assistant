#ifndef LOCKSCREEN_H
#define LOCKSCREEN_H

#include <QWidget>

class QTimer;
class QKeyEvent;
class QPaintEvent;

// 从 exe 同目录的 BGAssistant.ini 读取锁屏密码（AES 加密存储，不写注册表）。
// 未配置时返回默认密码 "a"。
QString lockScreenPassword();

// 设置锁屏密码：AES 加密后写入 ini。
void setLockScreenPassword(const QString &plain);

// 锁屏期间是否保持屏幕常亮（对应设置页「锁屏期间保持屏幕常亮」，存 ini）。
bool lockScreenKeepScreenOn();
void setLockScreenKeepScreenOn(bool on);

// 确保配置 ini 存在：无密码时写入默认密码（用于首次启动自动创建 ini）。
void ensureLockScreenConfig();

// 神隐锁屏：全屏近乎透明窗口（alpha=1，肉眼不可见），屏幕上不显示任何锁屏组件，
// 屏蔽键盘/鼠标操作；键盘输入由窗口自身 keyPressEvent 累积，盲打密码匹配即自动解锁。
// 仅在锁定时弹「已锁定」、解锁时弹「已解锁」提示。行为对齐原易语言版。
//
// 由命令行 superLock 参数以独立子进程启动，解锁后发出 unlocked() 信号、由调用方退出进程，
// 钩子/焦点/鼠标锁定等状态随进程退出而自动清理。
class LockScreen : public QWidget
{
    Q_OBJECT

public:
    explicit LockScreen();
    ~LockScreen() override;

    void setPassword(const QString &password);
    void showLockScreen();

signals:
    // 解锁成功（锁屏已解除）。锁屏子进程收到后退出进程。
    void unlocked();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;

private:
    QTimer *mouseLockTimer = nullptr;
    QString correctPassword;
    QString inputBuffer;   // 盲打密码累积缓冲区
    // 用 void* 保存 HHOOK / HWND：头文件里不引 windows.h，避免 min/max 宏污染所有包含者
    void *keyboardHook = nullptr;
    void *prevForegroundWindow = nullptr;

    void installHooks();
    void removeHooks();
    void doUnlock();
    void keepScreenOn(bool on);
    void showToast(const QString &text);
};

#endif // LOCKSCREEN_H
