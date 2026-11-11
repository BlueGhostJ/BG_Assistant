#ifndef SANDBOXIE_H
#define SANDBOXIE_H

#include <QList>
#include <QString>

// 一个沙箱的启动配置：来自 Sandboxie.ini 某个节里的一条 RunCommand。
//
// 配置文件里的写法是「键|路径」，路径落在沙箱内部布局上，例：
//     RunCommand=wegame.exe|"%BoxRoot%\drive\C\Program Files (x86)\WeGame\wegame.exe"
// 实际执行时要把沙箱布局反向映射回真实路径，再用 SandMan 拉起：
//     "C:\Program Files\Sandboxie-Plus\SandMan.exe" /box:WeGame "C:\Program Files (x86)\WeGame\wegame.exe"
struct SandboxEntry
{
    QString boxName;      // 节名，即沙箱名（方括号内的文本）
    QString menuTitle;    // 菜单项显示名：该节只有一条 RunCommand 时就是沙箱名，
                          // 有多条时是「沙箱名|键名」，否则同名项分不清
    QString program;      // RunCommand 竖线前的键（可执行文件名），仅用于展示排查
    QString rawPath;      // 竖线后的原始路径，未做任何映射
    QString targetPath;   // 映射到真实文件系统后的程序路径
    QString commandLine;  // 完整命令行，展示用；与 launch() 传给 SandMan 的参数一致
};

namespace Sandboxie {

// 配置文件固定位置（Sandboxie 的全局配置，不随用户目录变化）
QString iniPath();

// SandMan.exe 路径：在常见安装位置里找，都找不到时返回默认路径，交由启动时报错
QString sandManPath();

// 解析 ini，返回所有带 RunCommand 的沙箱，按沙箱名字母序排列（忽略大小写）。
// 一个节里有多条 RunCommand 时，它们会聚在一起连续出现，menuTitle 带上键名区分。
// 文件不存在/读不出内容时返回空列表，调用方据此隐藏菜单。
QList<SandboxEntry> loadEntries(const QString &iniPath);

// 把 RunCommand 里的沙箱路径映射成真实路径：
//   %BoxRoot%\drive\<盘符>\...     → <盘符>:\...
//   %BoxRoot%\user\current\...     → <当前用户目录>\...
//   其它（本来就是真实路径）        → 原样返回
QString mapPath(const QString &rawPath);

// 用 SandMan.exe 启动沙箱；失败时把原因写进 errorOut
bool launch(const SandboxEntry &entry, QString *errorOut = nullptr);

} // namespace Sandboxie

#endif // SANDBOXIE_H
