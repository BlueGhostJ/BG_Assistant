// windows.h 必须带着 NOMINMAX / WIN32_LEAN_AND_MEAN 引入，
// 否则 min/max 宏会污染本文件后续所有头文件（以及任何包含 lockscreen.h 的地方）。
#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef _WIN32_WINNT
#    define _WIN32_WINNT 0x0601   // Windows 7+，ITaskbarList 接口需要
#  endif
#endif

#include "lockscreen.h"
#include "aes.h"

#include <QApplication>
#include <QLabel>
#include <QTimer>
#include <QDebug>
#include <QGuiApplication>
#include <QScreen>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QSettings>
#include <QCoreApplication>
#include <QDir>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <shobjidl.h>
#  pragma comment(lib, "user32.lib")
#  pragma comment(lib, "ole32.lib")
#  pragma comment(lib, "uuid.lib")

// 从任务栏移除窗口按钮（用 ITaskbarList::DeleteTab）。
// 不使用 WS_EX_TOOLWINDOW，因为 tool window 无法被 SetForegroundWindow 带到前台，
// 会导致键盘焦点丢失、锁屏无法接收密码输入。
static void removeFromTaskbar(HWND hwnd)
{
    ITaskbarList *taskbar = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_ITaskbarList, reinterpret_cast<void **>(&taskbar));
    if (SUCCEEDED(hr) && taskbar) {
        taskbar->HrInit();
        taskbar->DeleteTab(hwnd);
        taskbar->Release();
    }
}

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

// 锁屏密码的 AES 密钥：与易语言版常量「神隐锁屏密码加密秘钥」一致，
// 值为 Qmx1ZUdob3N0（"BlueGhost" 的 Base64），12 字节 → keySize=3、nRounds=9（非标准 AES）。
static QString lockScreenAesKey()
{
    return QStringLiteral("Qmx1ZUdob3N0");
}

QString lockScreenPassword()
{
    QSettings ini(lockScreenIniPath(), QSettings::IniFormat);
    const QString stored = ini.value(QStringLiteral("LockScreen/Password")).toString();
    if (stored.isEmpty())
        return QString();   // 未设置密码

    return aesDecryptBase64(stored, lockScreenAesKey());
}

void setLockScreenPassword(const QString &plain)
{
    QSettings ini(lockScreenIniPath(), QSettings::IniFormat);
    ini.setValue(QStringLiteral("LockScreen/Password"),
                 aesEncryptBase64(plain, lockScreenAesKey()));
}

void ensureLockScreenConfig()
{
    // 首次启动时 ini 尚不存在：创建「无密码」的 ini 文件（密码键为空，表示尚未设置密码）。
    // 未设置密码时，锁屏会提示先设置密码（对齐原版逻辑，不再写入默认密码）。
    QSettings ini(lockScreenIniPath(), QSettings::IniFormat);
    if (!ini.contains(QStringLiteral("LockScreen/Password")))
        ini.setValue(QStringLiteral("LockScreen/Password"), QString());
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
    // 神隐：窗口近乎透明（alpha=1），透出真实桌面、屏幕上不显示任何锁屏组件；
    // alpha 不能为 0，否则 Windows 分层窗口会鼠标穿透到下层软件。
    setAttribute(Qt::WA_TranslucentBackground);
    setWindowModality(Qt::ApplicationModal);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);

    // 鼠标锁定：周期把光标拉回屏幕正中央（对齐原版“锁定鼠标线程”）。
    mouseLockTimer = new QTimer(this);
    mouseLockTimer->setInterval(20);
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

void LockScreen::setPassword(const QString &password)
{
    correctPassword = password;
}

void LockScreen::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    // alpha=1：肉眼不可见（保持神隐），但 alpha>0 使分层窗口的鼠标命中测试生效，拦截鼠标。
    QPainter painter(this);
    painter.fillRect(rect(), QColor(0, 0, 0, 1));
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

    inputBuffer.clear();

#ifdef Q_OS_WIN
    // 记录锁屏前的激活窗口句柄，解锁后恢复焦点（对齐原版 GetForegroundWindow）。
    prevForegroundWindow = reinterpret_cast<void*>(GetForegroundWindow());
#endif

    showFullScreen();
#ifdef Q_OS_WIN
    // 从任务栏移除按钮（用 ITaskbarList，不影响窗口焦点）。
    removeFromTaskbar((HWND)winId());
#endif
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
        BringWindowToTop(hwnd);
#endif

        showToast(QStringLiteral("已锁定"), QColor(255, 0, 0));
        qDebug() << "锁屏已激活 - 键盘+鼠标已捕获";
    });
}

void LockScreen::keyPressEvent(QKeyEvent *event)
{
    // 盲打密码：由锁屏窗口自身累积按键字符（grabKeyboard 会把键盘事件定向到本窗口，
    // 若走密码框则收不到输入）。匹配密码即解锁。
    if (event->key() == Qt::Key_Backspace) {
        if (!inputBuffer.isEmpty())
            inputBuffer.chop(1);
        event->accept();
        return;
    }

    const QString text = event->text();
    if (!text.isEmpty()) {
        inputBuffer += text;
        if (inputBuffer == correctPassword) {
            doUnlock();
            return;
        }
        // 逐字符前缀匹配（对齐原版）：输入必须从第一个字符开始匹配密码前缀，
        // 一旦不匹配立即清空，从头重新输入。
        if (!correctPassword.startsWith(inputBuffer))
            inputBuffer.clear();
    }
    event->accept();
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

void LockScreen::doUnlock()
{
    removeHooks();
    releaseKeyboard();
    releaseMouse();
    qApp->removeEventFilter(this);
    mouseLockTimer->stop();
    keepScreenOn(false);
    inputBuffer.clear();
    close();

#ifdef Q_OS_WIN
    // 解锁后把焦点还给锁屏前的激活窗口（对齐原版）。
    if (prevForegroundWindow) {
        SetForegroundWindow(reinterpret_cast<HWND>(prevForegroundWindow));
        prevForegroundWindow = nullptr;
    }
#endif

    showToast(QStringLiteral("已解锁"), QColor(0, 255, 0));
    // 延迟退出：让「已解锁」提示完整显示后再发出解锁信号、由调用方退出进程，
    // 否则立即 quit 会导致提示一闪而过甚至完全看不到。
    QTimer::singleShot(1000, this, [this]() {
        emit unlocked();
    });
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

void LockScreen::showToast(const QString &text, const QColor &color)
{
    QLabel *toast = new QLabel(text, nullptr);
    toast->setAttribute(Qt::WA_DeleteOnClose);
    toast->setAttribute(Qt::WA_ShowWithoutActivating);
    toast->setAttribute(Qt::WA_TranslucentBackground);
    toast->setFocusPolicy(Qt::NoFocus);
    toast->setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    toast->setAlignment(Qt::AlignCenter);
    toast->setStyleSheet(
        QStringLiteral("background: rgba(0,0,0,160); color: %1; font-size: 48px; font-weight: bold;"
                       " border-radius: 12px; padding: 16px 36px;").arg(color.name()));
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
