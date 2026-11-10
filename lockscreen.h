#ifndef LOCKSCREEN_H
#define LOCKSCREEN_H

#include <QWidget>
#include <QLineEdit>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

class LockScreen : public QWidget
{
    Q_OBJECT

public:
    explicit LockScreen();
    ~LockScreen();
    void setPassword(const QString &password);
    void showLockScreen();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;

private slots:
    void attemptUnlock();

private:
    QLineEdit *passwordEdit;
    QString correctPassword;
    HHOOK keyboardHook = nullptr;

    void setupUI();
    void installHooks();
    void removeHooks();
};

#endif // LOCKSCREEN_H
