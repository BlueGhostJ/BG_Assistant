#include "configform.h"
#include "ui_configform.h"

#include <QSettings>
#include <QCoreApplication>
#include <QDir>
#include <QIcon>
#include <QSignalBlocker>

#include "lockscreen.h"  // lockScreenKeepScreenOn / setLockScreenKeepScreenOn（ini 配置）

#ifdef Q_OS_WIN
namespace {

const char kRunKey[] =
    "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";

// 把注册表里的值和 applicationFilePath() 都规范化成同一种写法再比较：
// 去掉包裹引号、统一斜杠、去掉多余分隔符。比较时再忽略大小写。
// 否则引号/大小写/8.3 短路径任一差异都会误判成"未启用"。
QString normalizeAppPath(QString path)
{
    path = path.trimmed();
    if (path.size() >= 2 && path.startsWith(QLatin1Char('"')) && path.endsWith(QLatin1Char('"')))
        path = path.mid(1, path.size() - 2);
    return QDir::cleanPath(path);
}

QString autoStartCommand()
{
    QString appPath = QCoreApplication::applicationFilePath();
    appPath.replace(QLatin1Char('/'), QLatin1Char('\\'));
    appPath.prepend(QLatin1Char('"'));
    appPath.append(QLatin1Char('"'));
    return appPath;
}

void setAutoStart(bool enable)
{
    QSettings settings(QString::fromLatin1(kRunKey), QSettings::NativeFormat);
    const QString appName = QCoreApplication::applicationName();

    if (enable)
        settings.setValue(appName, autoStartCommand());
    else
        settings.remove(appName);
}

bool isAutoStartEnabled()
{
    QSettings settings(QString::fromLatin1(kRunKey), QSettings::NativeFormat);
    const QString stored = settings.value(QCoreApplication::applicationName()).toString();
    if (stored.isEmpty())
        return false;

    return QString::compare(normalizeAppPath(stored),
                            normalizeAppPath(QCoreApplication::applicationFilePath()),
                            Qt::CaseInsensitive) == 0;
}

} // namespace
#endif // Q_OS_WIN

ConfigForm::ConfigForm(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ConfigForm)
{
    ui->setupUi(this);
    this->setWindowIcon(QIcon(QString(":/icon/icons/config.png"))); // 标题栏icon
    this->setAttribute(Qt::WA_QuitOnClose, false); // 关闭窗口不退出

    loadSettings();

    // 显式连接 toggled：stateChanged(int) 在 Qt 6.7+ 已 deprecated，
    // 而 checkStateChanged 在 6.7 之前不存在，toggled 各版本都可用。
    connect(ui->autoStartCheckBox, &QCheckBox::toggled,
            this, &ConfigForm::applyAutoStart);
    connect(ui->keepScreenOnCheckBox, &QCheckBox::toggled,
            this, &ConfigForm::applyKeepScreenOn);
}

ConfigForm::~ConfigForm()
{
    delete ui;
}

void ConfigForm::loadSettings()
{
#ifdef Q_OS_WIN
    // 挡住信号：初始化时 setChecked() 不应反过来写注册表
    const QSignalBlocker blocker(ui->autoStartCheckBox);
    ui->autoStartCheckBox->setChecked(isAutoStartEnabled());
#else
    ui->autoStartCheckBox->setChecked(false);
    ui->autoStartCheckBox->setEnabled(false);
#endif

    // 锁屏期间保持屏幕常亮：纯 ini 配置，与平台无关
    const QSignalBlocker blocker2(ui->keepScreenOnCheckBox);
    ui->keepScreenOnCheckBox->setChecked(lockScreenKeepScreenOn());
}

void ConfigForm::applyAutoStart(bool enabled)
{
#ifdef Q_OS_WIN
    setAutoStart(enabled);
#else
    Q_UNUSED(enabled)
#endif
}

void ConfigForm::applyKeepScreenOn(bool enabled)
{
    setLockScreenKeepScreenOn(enabled);
}
