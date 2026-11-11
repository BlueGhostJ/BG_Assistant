#ifndef ELEVATION_H
#define ELEVATION_H

#include <QString>

// 管理员权限相关（Windows UAC）。
//
// 原版做法：托盘「系统路由管理」→ 以管理员模式创建进程（ShellExecute + runas），
// 新进程带 RouteManager 参数启动，参数处理里再打开窗口。本程序沿用同一套流程。

// 当前进程是否已拿到管理员权限（UAC 提升后的完整令牌）。
bool isProcessElevated();

// 以管理员身份重新启动本程序，arguments 为传给新进程的命令行参数。
// 成功返回 true；失败返回 false，errorOut 非空时回填原因（用户取消 UAC 时为空串）。
bool runAsAdmin(const QString &arguments, QString *errorOut = nullptr);

#endif // ELEVATION_H
