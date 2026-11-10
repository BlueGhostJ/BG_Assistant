#ifndef LOCKSCREEN_H
#define LOCKSCREEN_H

#include <QWidget>

class QLineEdit;
class QMouseEvent;

class LockScreen : public QWidget
{
    Q_OBJECT

public:
    explicit LockScreen();
    ~LockScreen() override;
    void setPassword(const QString &password);
    void showLockScreen();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;

private slots:
    void attemptUnlock();

private:
    QLineEdit *passwordEdit = nullptr;
    QString correctPassword;
    // 用 void* 保存 HHOOK：头文件里不引 windows.h，避免 min/max 宏污染所有包含者
    void *keyboardHook = nullptr;

    void setupUI();
    void installHooks();
    void removeHooks();
};

#endif // LOCKSCREEN_H
