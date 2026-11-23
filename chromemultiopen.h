#ifndef CHROMEMULTIOPEN_H
#define CHROMEMULTIOPEN_H

#include <QList>
#include <QString>
#include <QStringList>

// 一个 Chrome 多开配置 = 一套独立的数据目录 + 一组启动参数。
//
// 参数写法对齐原来的 bat 脚本：
//   chrome.exe --user-data-dir="<数据目录>" --disk-cache-dir="<临时目录>" [其它参数]
// 其中数据目录 = <数据根目录>\<名称>，临时缓存目录 = %TEMP%\<名称>，
// 与脚本里 %cd%\%batFileName% 和 %temp%\%batFileName% 的对应关系一致。
struct ChromeProfile
{
    // 代理方式，取值与界面上代理下拉框的顺序一致
    enum ProxyMode
    {
        ProxySystem = 0,    // 不传参数，跟随 Chrome 默认（系统代理）
        ProxyCustom = 1,    // --proxy-server=...
        ProxyDisabled = 2   // --no-proxy-server
    };

    QString name;                 // 必填；同时作为多开数据子目录名

    int proxyMode = ProxySystem;
    QString proxyServer;          // 如 socks5://127.0.0.1:1080

    bool useUserAgent = false;
    QString userAgent;            // --user-agent=...

    bool useAppMode = false;
    QString appUrl;               // --app=...（独立窗口模式）

    bool useWindowSize = false;
    int windowWidth = 1280;       // --window-size=宽,高
    int windowHeight = 720;

    bool useWindowPosition = false;
    int windowX = 0;              // --window-position=x,y
    int windowY = 0;

    bool startMaximized = false;  // --start-maximized
    bool startFullscreen = false; // --start-fullscreen
    bool kiosk = false;           // --kiosk（演示 / 无边框全屏）
    bool forceScale150 = false;   // --force-device-scale-factor=1.5

    bool useWindowName = false;
    QString windowName;           // --window-name=...（仅首次生效）
};

// Chrome 多开的配置、目录与启动逻辑。
// 配置写在 exe 同目录 BGAssistant.ini 的 [ChromeMultiOpen] 分组里，
// 数据目录默认是 exe 同目录的 chrome多开 文件夹（和原来的 bat 习惯一致）。
namespace ChromeMultiOpen {

QString iniPath();

// 依次枚举常见的 chrome.exe 安装位置，返回第一个真实存在的；都没找到返回空。
QString chromePath();
QStringList chromeCandidates();

QString defaultDataRoot();            // exe 同目录\chrome多开
QString dataRoot();                   // 用户指定过就用指定位置，否则用默认位置
void setDataRoot(const QString &path); // 传空表示恢复默认

QString dataDirFor(const QString &name);
QString tempDirFor(const QString &name);

QList<ChromeProfile> loadProfiles();
bool saveProfiles(const QList<ChromeProfile> &profiles);   // 写盘失败（目录只读等）返回 false

QString proxySummary(const ChromeProfile &profile);   // 列表里显示的代理说明
QString optionSummary(const ChromeProfile &profile);  // 列表里显示的其它选项摘要

QStringList buildArguments(const ChromeProfile &profile);
QString commandLine(const ChromeProfile &profile);    // 展示用（带引号）

bool isValidName(const QString &name, QString *reason);  // 名称同时是目录名，需按文件名规则校验

// 建好数据/临时目录后以独立进程启动 chrome.exe，失败时把原因写进 error。
bool launch(const ChromeProfile &profile, QString *error);

} // namespace ChromeMultiOpen

#endif // CHROMEMULTIOPEN_H
