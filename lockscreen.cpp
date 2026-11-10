#include "lockscreen.h"
#include <QVBoxLayout>
#include <QApplication>
#include <QLabel>
#include <QPushButton>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTimer>
#include <QDebug>
#include <QGuiApplication>
#include <QScreen>
#include <QWindow>
#include <QCoreApplication>
#include <QPainter>
#include <QPixmap>

#ifdef Q_OS_WIN
#pragma comment(lib, "user32.lib")

static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT *p = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        DWORD vk = p->vkCode;

        // Block Win keys (left and right)
        if (vk == VK_LWIN || vk == VK_RWIN)
            return 1;

        // Allow Ctrl+Alt+Del to pass through (system needs it)
        if (vk == VK_DELETE && (GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
            (GetAsyncKeyState(VK_MENU) & 0x8000))
            return CallNextHookEx(nullptr, nCode, wParam, lParam);

        // Block Alt+Tab, Alt+Esc, Alt+F4
        if ((GetAsyncKeyState(VK_MENU) & 0x8000) &&
            (vk == VK_TAB || vk == VK_ESCAPE || vk == VK_F4))
            return 1;

        // Block Ctrl+Esc
        if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) && vk == VK_ESCAPE)
            return 1;

        // Block Alt+Space (system menu)
        if ((GetAsyncKeyState(VK_MENU) & 0x8000) && vk == VK_SPACE)
            return 1;

        // Block Alt+Enter (fullscreen toggle)
        if ((GetAsyncKeyState(VK_MENU) & 0x8000) && vk == VK_RETURN)
            return 1;
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}
#endif

LockScreen::LockScreen() : QWidget(nullptr)
{
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint |
                   Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setWindowModality(Qt::ApplicationModal);
    setFocusPolicy(Qt::StrongFocus);

    setMouseTracking(true);
    setupUI();
}

LockScreen::~LockScreen()
{
    removeHooks();
    releaseKeyboard();
    releaseMouse();
    if (qApp)
        qApp->removeEventFilter(this);
}

void LockScreen::setupUI()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (screen) {
        QPixmap screenshot = screen->grabWindow(0);
        QPixmap bg(screenshot.size());
        bg.fill(Qt::transparent);
        QPainter painter(&bg);
        painter.drawPixmap(0, 0, screenshot);
        painter.fillRect(bg.rect(), QColor(0, 0, 0, 60));
        painter.end();
        QPalette p;
        p.setBrush(QPalette::Window, QBrush(bg));
        setPalette(p);
        setAutoFillBackground(true);
    }

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    QLabel *label = new QLabel("系统已锁定", this);
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet("color: white; font-size: 24px; margin-bottom: 20px; background: transparent;");

    passwordEdit = new QLineEdit(this);
    passwordEdit->setPlaceholderText("请输入解锁密码");
    passwordEdit->setEchoMode(QLineEdit::Password);
    passwordEdit->setAlignment(Qt::AlignCenter);
    passwordEdit->setFocusPolicy(Qt::StrongFocus);
    passwordEdit->setStyleSheet(
        "QLineEdit {"
        "  color: white; font-size: 18px;"
        "  background: rgba(255,255,255,0.2);"
        "  border: 1px solid gray;"
        "  padding: 8px;"
        "  min-width: 200px;"
        "}"
        );

    QPushButton *unlockButton = new QPushButton("解锁", this);
    unlockButton->setStyleSheet(
        "QPushButton {"
        "  color: white; font-size: 18px;"
        "  background: rgba(0,100,200,0.7);"
        "  padding: 8px;"
        "  min-width: 100px;"
        "}"
        );

    layout->addStretch();
    layout->addWidget(label, 0, Qt::AlignCenter);
    layout->addWidget(passwordEdit, 0, Qt::AlignCenter);
    layout->addSpacing(10);
    layout->addWidget(unlockButton, 0, Qt::AlignCenter);
    layout->addStretch();

    connect(unlockButton, &QPushButton::clicked, this, &LockScreen::attemptUnlock);
    connect(passwordEdit, &QLineEdit::returnPressed, this, &LockScreen::attemptUnlock);
}

void LockScreen::setPassword(const QString &password)
{
    correctPassword = password;
}

void LockScreen::installHooks()
{
#ifdef Q_OS_WIN
    if (!keyboardHook) {
        keyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc,
                                        GetModuleHandle(nullptr), 0);
        qDebug() << "低级键盘钩子安装:" << (keyboardHook ? "成功" : "失败");
    }
#endif
}

void LockScreen::removeHooks()
{
#ifdef Q_OS_WIN
    if (keyboardHook) {
        UnhookWindowsHookEx(keyboardHook);
        keyboardHook = nullptr;
        qDebug() << "键盘钩子已卸载";
    }
#endif
}

void LockScreen::showLockScreen()
{
    showFullScreen();
    raise();
    activateWindow();

    QTimer::singleShot(100, this, [this]() {
        qApp->installEventFilter(this);
        grabKeyboard();
        grabMouse();
        installHooks();

#ifdef Q_OS_WIN
        HWND hwnd = (HWND)this->winId();
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        SetForegroundWindow(hwnd);
#endif

        QTimer::singleShot(0, passwordEdit, [this]() {
            passwordEdit->setFocus();
        });
        qDebug() << "锁屏已激活 - 键盘+鼠标已捕获";
    });
}

void LockScreen::keyPressEvent(QKeyEvent *event)
{
    QWidget *focused = focusWidget();
    if (focused && focused != this) {
        QCoreApplication::sendEvent(focused, event);
        return;
    }
    QWidget::keyPressEvent(event);
}

void LockScreen::mousePressEvent(QMouseEvent *event)
{
    event->ignore();
}

bool LockScreen::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyPress) {
        QWidget *target = qobject_cast<QWidget*>(watched);
        if (target && target != this && !isAncestorOf(target)) {
            return true;
        }
        return false;
    }

    if (event->type() == QEvent::WindowActivate ||
        event->type() == QEvent::WindowDeactivate) {
        QTimer::singleShot(30, this, [this]() {
            if (isVisible()) {
                raise();
                activateWindow();
#ifdef Q_OS_WIN
                HWND hwnd = (HWND)this->winId();
                SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE);
                SetForegroundWindow(hwnd);
#endif
                grabKeyboard();
                grabMouse();
                passwordEdit->setFocus();
            }
        });
    }

    return QWidget::eventFilter(watched, event);
}

bool LockScreen::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
#ifdef Q_OS_WIN
    MSG *msg = static_cast<MSG*>(message);
    if (msg->message == WM_ACTIVATEAPP) {
        if (msg->wParam == FALSE) {
            QTimer::singleShot(20, this, [this]() {
                if (isVisible()) {
                    HWND hwnd = (HWND)this->winId();
                    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
                    SetForegroundWindow(hwnd);
                    ShowWindow(hwnd, SW_SHOWMAXIMIZED);
                    grabKeyboard();
                    grabMouse();
                    installHooks();
                }
            });
        }
    }
    if (msg->message == WM_WINDOWPOSCHANGING) {
        WINDOWPOS *wp = reinterpret_cast<WINDOWPOS*>(msg->lParam);
        wp->hwndInsertAfter = HWND_TOPMOST;
        wp->flags &= ~SWP_NOZORDER;
    }
#endif
    return QWidget::nativeEvent(eventType, message, result);
}

void LockScreen::attemptUnlock()
{
    if (passwordEdit->text() == correctPassword) {
        removeHooks();
        releaseKeyboard();
        releaseMouse();
        qApp->removeEventFilter(this);
        close();
        qDebug() << "解锁成功";
    } else {
        passwordEdit->clear();
        passwordEdit->setPlaceholderText("密码错误，请重试");
        passwordEdit->setFocus();
        qDebug() << "解锁失败";
    }
}
