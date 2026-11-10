#ifndef LOCKSCREEN_H
#define LOCKSCREEN_H

#include <QWidget>

class QLineEdit;
class QTimer;

// 从 exe 同目录的 BGAssistant.ini 读取锁屏密码（不写注册表）。
// 开发期间未在 ini 中配置时，默认返回写死的密码 "a"。
QString lockScreenPassword();

// 锁屏期间是否保持屏幕常亮（对应设置页「锁屏期间保持屏幕常亮」，存 ini）。
bool lockScreenKeepScreenOn();
void setLockScreenKeepScreenOn(bool on);

// 神隐锁屏：全屏透明窗口，屏幕上不显示任何锁屏组件（保持屏幕原样、肉眼不可见），
// 屏蔽键盘/鼠标操作；焦点锁定到隐藏的密码框，盲打密码，输入正确即自动解锁。
// 仅在锁定时弹「已锁定」、解锁时弹「已解锁」提示。行为对齐原易语言版。
class LockScreen : public QWidget
{
    Q_OBJECT

public:
    explicit LockScreen();
    ~LockScreen() override;

    void setPassword(const QString &password);
    void showLockScreen();

signals:
    // 锁屏状态变化：true=已锁定，false=已解锁。供托盘图标切换「锁/太极」图标。
    void lockStateChanged(bool locked);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;

private slots:
    void onPasswordChanged(const QString &text);
    void attemptUnlock();

private:
    QLineEdit *passwordEdit = nullptr;
    QTimer *mouseLockTimer = nullptr;
    QString correctPassword;
    // 用 void* 保存 HHOOK / HWND：头文件里不引 windows.h，避免 min/max 宏污染所有包含者
    void *keyboardHook = nullptr;
    void *prevForegroundWindow = nullptr;

    void setupUI();
    void installHooks();
    void removeHooks();
    void doUnlock();
    void keepScreenOn(bool on);
    void showToast(const QString &text);
};

#endif // LOCKSCREEN_H
