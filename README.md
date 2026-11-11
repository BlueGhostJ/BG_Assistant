# BG_Assistant

Windows 桌面助手（Qt 6 / MinGW）。常驻系统托盘，提供开机自启、系统锁定与神隐锁屏（全屏遮屏、盲打密码解锁）。

---

## 功能

| 功能 | 说明 |
| --- | --- |
| 系统托盘 | 常驻托盘（太极图标动画），左键弹出菜单 |
| 开机自启 | 写入注册表 `Run` 项，随系统启动 |
| 系统路由管理 | 以管理员身份添加/删除静态路由，原生表格查看系统路由表（托盘「系统」子菜单） |
| 沙箱启动 | 解析 `C:\Windows\Sandboxie.ini` 的 `RunCommand`，托盘「沙箱」子菜单一键启动对应沙箱 |
| 系统锁定 | 调用系统 `LockWorkStation` 锁定 Windows |
| 神隐锁屏 | 全屏遮屏：屏幕保持原样、肉眼不可见，盲打密码解锁（详见下文） |
| 锁屏密码 | AES 加密存储于本地 ini，可在设置页修改 |

---

## 环境要求

| 依赖 | 版本 | 说明 |
| --- | --- | --- |
| Qt | 6.9.0（MinGW 64-bit） | 仅支持 Qt 6，已不再兼容 Qt 5 |
| CMake | 3.16 及以上 | 可使用 Qt 安装包自带的 `Tools\CMake_64` |
| MinGW | 13.1.0（随 Qt 安装） | 位于 `Tools\mingw1310_64` |

以下步骤假定 Qt 安装在 `D:\Programs\Qt`，如实际路径不同请自行替换。

---

## 编译步骤

### 1. 打开命令行

使用开始菜单中的 **「Qt 6.9.0 (MinGW 13.1.0 64-bit)」** 快捷方式打开命令提示符。

> 该快捷方式只会把 **Qt 自身的 bin** 加入 `PATH`，**不包含** CMake 与 MinGW 工具链，因此还需要执行下一步。

### 2. 补充 CMake 与 MinGW 到 PATH

```bat
for /d %i in ("D:\Programs\Qt\Tools\mingw*") do @set "PATH=%~i\bin;%PATH%"
set "PATH=D:\Programs\Qt\Tools\CMake_64\bin;%PATH%"
```

验证工具链是否就绪，以下三条命令都应有版本输出：

```bat
cmake --version
g++ --version
mingw32-make --version
```

### 3. 配置并编译

```bat
cmake -B build -G "MinGW Makefiles" ^
      -DCMAKE_PREFIX_PATH=D:\Programs\Qt\6.9.0\mingw_64 ^
      -DCMAKE_BUILD_TYPE=Release

cmake --build build
```

参数说明：

- `-DCMAKE_PREFIX_PATH`：指向 Qt 的 **kit 目录**（`6.9.0\mingw_64`），不是 `Tools\` 下的目录，用于让 CMake 找到 `Qt6Config.cmake`。
- `-DCMAKE_BUILD_TYPE`：`MinGW Makefiles` 属于单配置生成器，构建类型必须在配置阶段指定。

编译产物：`build\BGAssistant.exe`

### 4. 重新构建

若此前配置失败过，`build` 目录中可能残留错误的 `CMakeCache.txt`，先清理再重新配置：

```bat
rmdir /s /q build
```

---

## 部署（打包为独立目录）

编译产出的 exe 依赖 Qt 的动态库，直接拷到没装 Qt 的机器上无法运行。用 `windeployqt` 把所需的 DLL 与插件复制到 exe 同级目录：

```bat
cd /d D:\Workspaces\BG_Assistant\build
windeployqt --release --compiler-runtime BGAssistant.exe
```

参数说明：

- `--release`：复制 Release 版 Qt 库。若构建的是 Debug 版本，改用 `--debug`。
- `--compiler-runtime`：连同 MinGW 运行时（`libgcc_s_seh-1.dll`、`libstdc++-6.dll`、`libwinpthread-1.dll`）一并复制。缺少它们时，程序在干净的机器上会直接启动失败，且往往没有明确报错。

执行后 `build\` 目录会补入：

```
BGAssistant.exe
Qt6Core.dll  Qt6Gui.dll  Qt6Widgets.dll
libgcc_s_seh-1.dll  libstdc++-6.dll  libwinpthread-1.dll
platforms\qwindows.dll
imageformats\   styles\   tls\   ...
```

整个 `build\` 目录即可作为免安装的绿色包分发。

注意事项：

- **exe 文件名是 `BGAssistant.exe`（无下划线）**，由 `CMakeLists.txt` 中的 `project(BGAssistant ...)` 决定。若写成 `BG_Assistant.exe`，`windeployqt` 会报 `Cannot find file`。
- `windeployqt` 位于 Qt 的 `bin` 目录（`D:\Programs\Qt\6.9.0\mingw_64\bin`），Qt 命令行快捷方式已将其加入 `PATH`。
- 目录分发后位置变化，需重新勾选「开机自启」，原因见下节。

---

## 运行

直接运行 `build\BGAssistant.exe`，程序常驻系统托盘（太极图标）。

托盘左键单击弹出菜单：

| 菜单项 | 功能 |
| --- | --- |
| 设置 | 打开设置窗口，可开关「开机自启」「锁屏期间保持屏幕常亮」，修改锁屏密码 |
| 系统 › 系统路由管理 | 以管理员身份打开系统路由管理窗口（新增） |
| 沙箱 › &lt;沙箱名&gt; | 启动 Sandboxie 沙箱里配置的程序（解析 `C:\Windows\Sandboxie.ini`） |
| 锁定 | 调用系统 `LockWorkStation` 锁定 Windows |
| 遮屏 | 启动神隐锁屏（独立子进程） |
| 退出 | 结束程序 |

### 开机自启

勾选后写入注册表：

```
HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run
```

**注意**：写入的是当前运行中 exe 的绝对路径。若从 `build\` 目录勾选，之后移动或删除该目录，自启项会失效，需重新勾选。

---

## 神隐锁屏

点击托盘「遮屏」后，程序以 `superLock` 命令行参数启动一个**独立子进程**执行锁屏，解锁后子进程退出。

- **视觉**：锁屏窗口全屏近乎透明（alpha=1），屏幕保持原样、肉眼看不到任何锁屏组件。
- **输入**：键盘盲打密码，采用逐字符前缀匹配——从第一个字符开始比对，一旦不匹配立即清空重输；输入正确即自动解锁（无需回车/按钮）。
- **屏蔽**：低级键盘钩子屏蔽 Win / Ctrl / Alt 及其组合键（Ctrl+Esc、Alt+Tab、Alt+F4、Ctrl+空格、Ctrl+Shift 等）；鼠标锁定周期把光标拉回屏幕正中央。
- **托盘**：锁屏子进程在托盘区额外显示一个「锁」图标（父进程的太极图标保持不变）；解锁后子进程退出、锁图标随之消失。
- **提示**：锁定时弹「已锁定」、解锁时弹「已解锁」，约 1 秒后自动消失。

### 锁屏密码

- 密码保存在 exe 同目录的 `BGAssistant.ini`（`[LockScreen]` 节的 `Password` 键），不再写注册表。
- 采用 **AES-ECB + PKCS7** 加密后以 Base64 存储，密钥与原易语言版「神隐锁屏密码加密秘钥」常量一致（12 字节，非标准 AES，keySize=3、nRounds=9）。
- 首次启动自动创建「无密码」的 ini 文件；未设置密码时点击「遮屏」会提示「尚未设置解锁密码！」并自动打开设置界面。
- 修改密码：设置页「锁屏密码」分组，需依次校验「新密码不能为空」「两次新密码一致」「原密码正确」后加密保存；首次设置密码时隐藏「原密码」输入项。

---

## 系统路由管理

托盘菜单「系统 → 系统路由管理」。用于配置发往不同 IP 段的数据经由不同网关转发（原易语言版同名功能的重构）。

### 提权方式

添加/删除路由需要管理员权限，启动方式与原版一致：**新开一个进程，在新进程里提权**。

1. 主进程（托盘）用 `TokenElevation` 判断自己是否已是管理员。
2. 不是管理员 → `ShellExecuteEx` + `runas` 重新启动自身，参数 `RouteManager`，触发 UAC。
3. 新进程识别到 `RouteManager` 参数后只显示「系统路由管理」窗口、**不创建托盘图标**，关闭窗口即退出。
4. 主进程本身已以管理员运行时直接在当前进程弹窗，不再多开一个进程。
5. 用户点了 UAC 的「否」时静默返回；其它失败（例如 exe 被占用）弹提示说明原因。

### 界面

与易语言原版一致：顶部两行说明、三行输入（需要配置的网段 / 该网段的子网掩码 / 该网段要使用的网关）、「是否长效路由」复选框、添加路由 / 删除路由两行、底部「显示系统路由表」。

- 三个 IP 输入框仿系统 IP 地址控件实现（四段 + 点号）：段内满 3 位自动跳下一段，点号 / ←→ / Tab 在段间移动，支持直接粘贴整段 IP。
- 掩码按网段自动推导：`0.x` → `0.0.0.0`、A 类 → `255.0.0.0`、B 类 → `255.255.0.0`、C 类 → `255.255.255.0`、D/E 类 → `255.255.255.255`。**手动改过掩码之后就不再自动覆盖。**
- 添加会校验网段/掩码/网关是否填全；删除只要求网段（与原版一致，掩码网关忽略）。

### 与原版的实现差异

命令本身与原版相同：`route [-p] add <网段> mask <掩码> <网关> metric 1`、`route delete <网段>`（`-p` 按官方语法放在子命令前，原版写在 `add` 后面）。差异只在外围：

| 环节 | 原版 | 本版 |
| --- | --- | --- |
| 执行命令 | `cmd.exe /c route ...`，会闪控制台窗口 | 直接调用 `route.exe`，`CREATE_NO_WINDOW` 隐藏窗口 |
| 结果校验 | `route print > %temp%\RoutePrint.txt` 再读文本 | `GetIpForwardTable2` 直接查路由表，不产生临时文件 |
| 显示路由表 | 导出 `%temp%\RoutePrint.txt`，用 Notepad++ 打开 | 原生「系统路由表」窗口：目标网络 / 掩码 / 网关 / 接口 / 跃点数 / 长效，可刷新 |
| 退出清理 | 关闭窗口时删除临时文件 | 没有临时文件 |

「系统路由表」窗口里的「在链路上」等价于 `route print` 的 `On-link`（网关为 `0.0.0.0` 的直连网段）。

---

## 沙箱启动

托盘一级菜单「沙箱」（位于「系统」下方）。菜单内容全部来自 Sandboxie 的全局配置 `C:\Windows\Sandboxie.ini`：每个 `RunCommand` 就是一条可启动项，一个节只写了一条时菜单项就是节名（方括号里的文本），写了多条时用 `节名|键名` 区分。

### 菜单结构

```
沙箱
├── 显示启动命令      ← 列出每个沙箱的 名称 → 启动命令（可复制，用于排查）
├── ────────────
├── apifox
├── LocalSend
├── ns_usbloader
├── tabby
├── WeGame
└── WinMerge
```

- 沙箱项**按名字字母序排列（忽略大小写）**，与 ini 里的书写顺序无关；同一个沙箱的多条 `RunCommand` 聚在一起，再按键名排序。
- 一个沙箱只配了一条 `RunCommand` 时，菜单项就是沙箱名；配了多条时显示为 `沙箱名|键名`（键名即 `RunCommand=` 后面竖线前那段，例如 `WeGame|wegame.exe`），否则菜单里会出现多个同名项分不清。
- 配置文件不存在、或没有任何带 `RunCommand` 的沙箱时，**连「沙箱」一级菜单一起不显示**，托盘上不会出现空菜单。
- 每次弹出托盘菜单前都会重新解析一次 ini，手工改了配置后不用重启，所以菜单里**不设单独的「刷新」项**。
- 未做映射的 `%BoxRoot%` 子布局原样传给 SandMan，由 Sandboxie 自己展开。

### 路径映射规则

ini 里的 `RunCommand` 写的是**沙箱内部**的落盘路径，启动前要反向映射回真实路径：

| ini 里的写法 | 实际执行 |
| --- | --- |
| `RunCommand=wegame.exe\|"%BoxRoot%\drive\C\Program Files (x86)\WeGame\wegame.exe"` | `SandMan.exe /box:WeGame "C:\Program Files (x86)\WeGame\wegame.exe"` |
| `RunCommand=Tabby.exe\|"%BoxRoot%\drive\D\Programs\tabby\Tabby.exe"` | `SandMan.exe /box:tabby "D:\Programs\tabby\Tabby.exe"` |
| `RunCommand=Apifox.exe\|"%BoxRoot%\user\current\AppData\Local\Programs\Apifox\Apifox.exe"` | `SandMan.exe /box:apifox "C:\Users\<用户>\AppData\Local\Programs\Apifox\Apifox.exe"` |
| `RunCommand=WinMergeU.exe\|"D:\Programs\WinMerge\WinMergeU.exe"` | `SandMan.exe /box:WinMerge "D:\Programs\WinMerge\WinMergeU.exe"`（本来就是真实路径，原样使用） |

即：

- `%BoxRoot%\drive\<盘符>\<其余>` → `<盘符>:\<其余>`
- `%BoxRoot%\user\current\<其余>` → `<当前用户目录>\<其余>`
- 不带 `%BoxRoot%` 的路径 → 原样使用

完整命令行格式：`"<SandMan.exe 路径>" /box:<沙箱名> "<映射后的程序路径>"`。

### 实现要点

- `SandMan.exe` 依次在 `%ProgramW6432%`、`%ProgramFiles%`、`%ProgramFiles(x86)%` 下的 `Sandboxie-Plus\` / `Sandboxie\` 里查找，找不到时按默认路径 `C:\Program Files\Sandboxie-Plus\SandMan.exe` 启动并弹提示。
- ini 编码自动识别：带 BOM 的 UTF-16 LE/BE、无 BOM 的 UTF-16、UTF-8，最后回退到系统本地编码（简中为 GBK）。**Sandboxie 默认写的是 UTF-16 LE**，按 UTF-8 硬读会全是乱码。
- 启动用 `QProcess::startDetached` 传参数列表，路径里的空格不需要手工转义；展示用的命令行与实际参数一一对应。
- 同一沙箱写了多条 `RunCommand` 时，每条都是独立的菜单项（名称加 `|键名` 后缀），不会互相覆盖；完全相同的重复行（同节、同键、同路径）只保留一条。
- 排序用 `std::stable_sort` + `Qt::CaseInsensitive` 比较：仅大小写不同的名字保持 ini 里的先后顺序。

---

## 注意事项

- **神隐锁屏不是安全锁。** 它用于防止他人误操作键盘/鼠标，无法阻止任务管理器结束进程、`Ctrl+Alt+Del`、`Win+L` 等系统级操作；多显示器环境下目前仅覆盖主屏。
- **托盘图标动画持续播放是有意设计。** 太极图标以 40ms 间隔循环播放（`main.cpp` 中的 `QTimer`），常驻重绘属于预期行为，不作为待优化项，代码审查时无需再提出。
- `icons\app.ico` 通过 `app.rc.in` 生成 `app.rc` 后编入 exe 的资源段，这是 exe 图标生效的唯一途径（CMake 没有 `WINDOWS_ICON` 属性）。

---

## 目录结构

```
.
├── CMakeLists.txt      构建配置
├── app.rc.in           exe 图标资源模板（由 CMake 生成 app.rc）
├── main.cpp            程序入口、托盘与菜单、锁屏子进程分支
├── mainwindow.*        主窗口（预留，当前未显示）
├── configform.*        设置窗口（开机自启、锁屏常亮、锁屏密码）
├── lockscreen.*        神隐锁屏（全屏遮屏、盲打密码解锁）
├── routemanager.*      系统路由管理窗口（添加/删除路由）
├── routetabledialog.*  系统路由表窗口（原生表格显示）
├── routequery.*        读取 IPv4 路由表（IP Helper API + 长效路由注册表）
├── sandboxie.*         解析 Sandboxie.ini、路径映射、启动沙箱
├── sandboxmenu.*       托盘「沙箱」菜单（含「显示启动命令」窗口）
├── ipaddressedit.*     仿系统 IP 地址控件的四段输入框
├── elevation.*         管理员提权（ShellExecuteEx runas）
├── aes.*               AES-ECB 加密（纯 C++ 实现，无外部依赖）
├── resources.qrc       图标资源清单
└── icons/              图标资源
```
