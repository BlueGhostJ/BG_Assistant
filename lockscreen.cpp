// windows.h 必须带着 NOMINMAX / WIN32_LEAN_AND_MEAN 引入，
// 否则 min/max 宏会污染本文件后续所有头文件（以及任何包含 lockscreen.h 的地方）。
#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#endif

#include "lockscreen.h"

#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QDebug>
#include <QGuiApplication>
#include <QScreen>
#include <QMouseEvent>
#include <QSettings>
#include <QCoreApplication>
#include <QDir>

#ifdef Q_OS_WIN
#  include <windows.h>
#  pragma comment(lib, "user32.lib")

// 低级键盘钩子：屏蔽 Win / Ctrl / Alt 及其组合键，
// 使锁屏期间键盘只能用于盲打解锁密码（对齐原易语言版“挂机锁屏蔽键盘实现”）。
static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT *p = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        DWORD vk = p->vkCode;

        // 屏蔽 Win 键（左/右）
        if (vk == VK_LWIN || vk == VK_RWIN)
            return 1;

        // Ctrl+Alt+Del 属于系统安全桌面（SAS），低级钩子无法拦截，
        // 放行交由系统处理，避免卡键。
        if (vk == VK_DELETE && (GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
            (GetAsyncKeyState(VK_MENU) & 0x8000))
            return CallNextHookEx(nullptr, nCode, wParam, lParam);

        const bool ctrlDown = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool altDown  = (GetAsyncKeyState(VK_MENU)    & 0x8000) != 0;

        // 屏蔽裸 Ctrl / Alt：覆盖其所有组合键（对齐原版的屏蔽左/右 Ctrl、左/右 Alt）
        if (vk == VK_LCONTROL || vk == VK_RCONTROL)
            return 1;
        if (vk == VK_LMENU || vk == VK_RMENU)
            return 1;

        // 屏蔽 Ctrl+Esc
        if (ctrlDown && vk == VK_ESCAPE)
            return 1;

        // 屏蔽 Alt+Tab / Alt+Esc / Alt+F4 / Alt+Space / Alt+Enter
        if (altDown && (vk == VK_TAB || vk == VK_ESCAPE || vk == VK_F4 ||
                        vk == VK_SPACE || vk == VK_RETURN))
            return 1;

        // 屏蔽 Ctrl+空格、Ctrl+Shift（对齐原版）
        if (ctrlDown && (vk == VK_SPACE || vk == VK_LSHIFT || vk == VK_RSHIFT))
            return 1;
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}
#endif

// 配置统一存放在 exe 同目录的 BGAssistant.ini（不写注册表）。
static QString lockScreenIniPath()
{
    return QCoreApplication::applicationDirPath()
           + QDir::separator() + QStringLiteral("BGAssistant.ini");
}

QString lockScreenPassword()
{
    QSettings ini(lockScreenIniPath(), QSettings::IniFormat);
    const QString pwd = ini.value(QStringLiteral("LockScreen/Password")).toString();
    // 开发期间未配置时回退到写死的 "a"，暂不加密。
    return pwd.isEmpty() ? QStringLiteral("a") : pwd;
}

bool lockScreenKeepScreenOn()
{
    QSettings ini(lockScreenIniPath(), QSettings::IniFormat);
    return ini.value(QStringLiteral("LockScreen/KeepScreenOn"), false).toBool();
}

void setLockScreenKeepScreenOn(bool on)
{
    QSettings ini(lockScreenIniPath(), QSettings::IniFormat);
    ini.setValue(QStringLiteral("LockScreen/KeepScreenOn"), on);
}

LockScreen::LockScreen() : QWidget(nullptr)
{
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint |
                   Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_DeleteOnClose, false);
    // 神隐：窗口全透明（透出真实桌面），锁屏期间屏幕上不显示任何锁屏组件。
    setAttribute(Qt::WA_TranslucentBackground);
    setWindowModality(Qt::ApplicationModal);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);

    setupUI();

    // 鼠标锁定：周期把光标拉回屏幕正中央（对齐原版“锁定鼠标线程”）。
    mouseLockTimer = new QTimer(this);
    mouseLockTimer->setInterval(50);
    connect(mouseLockTimer, &QTimer::timeout, this, [this]() {
#ifdef Q_OS_WIN
        // SetCursorPos 接收物理像素坐标，直接用系统主屏物理尺寸取中心，
        // 避免 Qt 逻辑坐标在高 DPI 缩放（>100%）下偏移到左上。
        const int cx = GetSystemMetrics(SM_CXSCREEN);
        const int cy = GetSystemMetrics(SM_CYSCREEN);
        SetCursorPos(cx / 2, cy / 2);
#endif
    });
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
    // 锁屏期间屏幕上看不到任何组件（神隐）：
    // 密码框视觉不可见（透明、无边框、透明文字与光标、1x1 放在窗口角落），
    // 仅作为焦点控件接收盲打输入，输入内容匹配密码时由 textChanged 自动解锁。
    passwordEdit = new QLineEdit(this);
    passwordEdit->setEchoMode(QLineEdit::Password);
    passwordEdit->setFocusPolicy(Qt::StrongFocus);
    passwordEdit->setStyleSheet(
        "QLineEdit { background: transparent; border: none;"
        " color: transparent; caret-color: transparent;"
        " selection-background-color: transparent; }");
    passwordEdit->resize(1, 1);

    connect(passwordEdit, &QLineEdit::textChanged, this, &LockScreen::onPasswordChanged);

    // 解锁按钮：保留但不可见（原版锁屏界面无任何可见组件），
    // 仅作为手动解锁的兜底；正常解锁由盲打密码自动触发。
    QPushButton *unlockButton = new QPushButton(QStringLiteral("解锁"), this);
    unlockButton->hide();
    connect(unlockButton, &QPushButton::clicked, this, &LockScreen::attemptUnlock);
}

void LockScreen::setPassword(const QString &password)
{
    correctPassword = password;
}

void LockScreen::installHooks()
{
#ifdef Q_OS_WIN
    if (!keyboardHook) {
        keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc,
                                         GetModuleHandleW(nullptr), 0);
        qDebug() << "低级键盘钩子安装:" << (keyboardHook ? "成功" : "失败");
    }
#endif
}

void LockScreen::removeHooks()
{
#ifdef Q_OS_WIN
    if (keyboardHook) {
        UnhookWindowsHookEx(static_cast<HHOOK>(keyboardHook));
        keyboardHook = nullptr;
        qDebug() << "键盘钩子已卸载";
    }
#endif
}

void LockScreen::showLockScreen()
{
    if (isVisible()) {
        raise();
        activateWindow();
        return;
    }

    // 清空上次残留输入：锁屏是常驻窗口反复复用，
    // 残留密码会让第二次锁屏"敲一字符再退格"即可解锁，必须每次从空开始。
    passwordEdit->clear();

#ifdef Q_OS_WIN
    // 记录锁屏前的激活窗口句柄，解锁后恢复焦点（对齐原版 GetForegroundWindow）。
    prevForegroundWindow = reinterpret_cast<void*>(GetForegroundWindow());
#endif

    showFullScreen();
    raise();
    activateWindow();

    QTimer::singleShot(100, this, [this]() {
        qApp->installEventFilter(this);
        grabKeyboard();
        grabMouse();
        installHooks();
        mouseLockTimer->start();
        // 屏幕常亮为可选设置项（存 ini），仅在用户勾选时开启。
        if (lockScreenKeepScreenOn())
            keepScreenOn(true);

#ifdef Q_OS_WIN
        HWND hwnd = (HWND)this->winId();
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        SetForegroundWindow(hwnd);
#endif

        // 焦点锁定到密码框：锁屏期间持续保证键盘输入进入密码框。
        QTimer::singleShot(0, passwordEdit, [this]() {
            passwordEdit->setFocus();
        });

        showToast(QStringLiteral("已锁定"));
        emit lockStateChanged(true);
        qDebug() << "锁屏已激活 - 键盘+鼠标已捕获";
    });
}

void LockScreen::mousePressEvent(QMouseEvent *event)
{
    // 顶级窗口没有父窗口可传，ignore() 没有任何作用。
    // 这里显式吞掉点击，语义是"遮罩层吃掉空白处点击"。
    event->accept();
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
                    grabKeyboard();
                    grabMouse();
                    installHooks();
                }
            });
        }
    }
    if (msg->message == WM_WINDOWPOSCHANGING) {
        WINDOWPOS *wp = reinterpret_cast<WINDOWPOS*>(msg->lParam);
        // 只在本次调整确实要改变 z-order 时才强制置顶，
        // 否则会连带影响后续任何子窗口/弹窗的层级。
        if (wp && !(wp->flags & SWP_NOZORDER)) {
            wp->hwndInsertAfter = HWND_TOPMOST;
            wp->flags &= ~SWP_NOZORDER;
        }
    }
#endif
    return QWidget::nativeEvent(eventType, message, result);
}

void LockScreen::onPasswordChanged(const QString &text)
{
    // 对齐原版：输入内容一旦等于锁屏密码即自动解锁（无需回车/按钮）。
    if (!correctPassword.isEmpty() && text == correctPassword)
        doUnlock();
}

void LockScreen::attemptUnlock()
{
    if (passwordEdit->text() == correctPassword) {
        doUnlock();
    } else {
        passwordEdit->clear();
        passwordEdit->setPlaceholderText(QStringLiteral("密码错误，请重试"));
        passwordEdit->setFocus();
        qDebug() << "解锁失败";
    }
}

void LockScreen::doUnlock()
{
    removeHooks();
    releaseKeyboard();
    releaseMouse();
    qApp->removeEventFilter(this);
    mouseLockTimer->stop();
    keepScreenOn(false);
    passwordEdit->clear();  // 清空输入，防止二次锁屏被残留密码一键解锁
    close();

#ifdef Q_OS_WIN
    // 解锁后把焦点还给锁屏前的激活窗口（对齐原版）。
    if (prevForegroundWindow) {
        SetForegroundWindow(reinterpret_cast<HWND>(prevForegroundWindow));
        prevForegroundWindow = nullptr;
    }
#endif

    showToast(QStringLiteral("已解锁"));
    emit lockStateChanged(false);
    qDebug() << "解锁成功";
}

void LockScreen::keepScreenOn(bool on)
{
#ifdef Q_OS_WIN
    // 锁屏期间保持屏幕常亮（对齐原版“屏幕常亮时钟”，由设置项控制）：
    // 阻止系统在锁屏期间进入待机/关闭显示器，解锁后复位。
    SetThreadExecutionState(on ? (ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED)
                               : ES_CONTINUOUS);
#else
    Q_UNUSED(on)
#endif
}

void LockScreen::showToast(const QString &text)
{
    QLabel *toast = new QLabel(text, nullptr);
    toast->setAttribute(Qt::WA_DeleteOnClose);
    toast->setAttribute(Qt::WA_ShowWithoutActivating);
    toast->setAttribute(Qt::WA_TranslucentBackground);
    toast->setFocusPolicy(Qt::NoFocus);
    toast->setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    toast->setAlignment(Qt::AlignCenter);
    toast->setStyleSheet(
        "background: rgba(0,0,0,160); color: white; font-size: 30px;"
        " border-radius: 10px; padding: 14px 28px;");
    toast->adjustSize();

    QScreen *screen = QGuiApplication::primaryScreen();
    if (screen) {
        const QRect geo = screen->geometry();
        toast->move(geo.center().x() - toast->width() / 2,
                    geo.center().y() - toast->height() / 2);
    }

    toast->show();
#ifdef Q_OS_WIN
    SetWindowPos((HWND)toast->winId(), HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
#endif
    QTimer::singleShot(1000, toast, &QObject::deleteLater);
}
