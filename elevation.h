#ifndef ELEVATION_H
#define ELEVATION_H

#include <QString>

// Windows 平台相关的少量封装：管理员提权（UAC）与系统程序路径。
//
// 原版做法：托盘「系统路由管理」→ 以管理员模式创建进程（ShellExecute + runas），
// 新进程带 RouteManager 参数启动，参数处理里再打开窗口。本程序沿用同一套流程。

// 当前进程是否已拿到管理员权限（UAC 提升后的完整令牌）。
bool isProcessElevated();

// 以管理员身份重新启动本程序，arguments 为传给新进程的命令行参数。
// 成功返回 true；失败返回 false，errorOut 非空时回填原因（用户取消 UAC 时为空串）。
bool runAsAdmin(const QString &arguments, QString *errorOut = nullptr);

// 取 System32 目录下系统程序的绝对路径（GetSystemDirectoryW 解析，不硬编码盘符）。
//
// 必须用绝对路径：CreateProcess 解析非绝对路径时的搜索顺序是
//   1. 应用程序自身所在目录  2. 当前目录  3. System32  4. Windows 目录  5. PATH
// 也就是说，只要有人往 BGAssistant.exe 同目录放一个同名 exe，被执行的就是那个假文件；
// 路由管理跑在提权子进程里，等于把管理员权限交给了同目录下的任意可执行文件。
//
// 这里没用 %SystemRoot% 环境变量：它可被 HKCU\Environment 覆盖并被新进程继承，
// 相当于把「往 exe 目录投毒」换成「往环境变量投毒」，问题没消失。
//
// 解析失败返回空串（不退回相对名）。
QString system32Path(const QString &fileName);

#endif // ELEVATION_H
