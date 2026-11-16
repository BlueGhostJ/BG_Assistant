#ifndef SYSTEMPATHS_H
#define SYSTEMPATHS_H

#include <QString>

// 取 System32 目录下系统程序的绝对路径（GetSystemDirectoryW 解析，不硬编码盘符）。
//
// 为什么必须用绝对路径：CreateProcess 解析非绝对路径时的搜索顺序是
//   1) 应用程序自身所在目录  2) 当前目录  3) System32  4) Windows 目录  5) PATH
// 也就是说，只要有人往 BGAssistant.exe 同目录放一个同名的 route.exe，
// 点「添加路由」时被执行的就是那个假文件；而路由管理跑在提权子进程里，
// 等于把管理员权限交给了同目录下的任意可执行文件。
//
// 解析失败返回空串（不退回相对名，避免把漏洞又带回来）。
QString system32Path(const QString &fileName);

#endif // SYSTEMPATHS_H
