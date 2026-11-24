#ifndef APPCONFIG_H
#define APPCONFIG_H

#include <QString>

// 程序自己的配置放在哪：
//   · exe 同目录已经有 BGAssistant.ini → 继续用 exe 同目录（绿色版，exe + ini 一起拷走就能带着配置走）；
//   · 没有 → 用 %LOCALAPPDATA%\Programs\BGAssistant\BGAssistant.ini（exe 放在 Program Files 这类只读目录时也能保存）。
// 判断只看「exe 同目录有没有这个文件」，所以用户把 ini 放到 exe 旁边就会自动切回绿色版布局。
namespace AppConfig {

QString installDataDir();   // %LOCALAPPDATA%\Programs\BGAssistant（LOCALAPPDATA 缺失时按用户目录兜底）
QString exeDirIniPath();    // exe 同目录的 ini 路径（不一定存在）
QString iniPath();          // 实际使用的 ini 路径（按上面的规则判断）

// 写配置前调用：确保 ini 所在目录存在（首次运行时要在 LOCALAPPDATA 下建目录）。
// 失败时把目录路径写进 error。
bool ensureIniDir(QString *error = nullptr);

} // namespace AppConfig

#endif // APPCONFIG_H
