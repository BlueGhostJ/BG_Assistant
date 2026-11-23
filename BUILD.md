# 构建与开发说明

面向开发者：环境要求、动态 / 静态构建、部署、静态 Qt 编译、常见错误，以及部分实现说明。

普通使用请看 [README.md](README.md)，不需要读本文。

---

## 环境要求

| 依赖 | 版本 | 说明 |
| --- | --- | --- |
| Qt（动态构建） | 6.12.0（MinGW 64-bit） | 安装器装的 kit，`build-dynamic\` 用它 |
| Qt 源码（静态构建） | `qt-everywhere-src-6.12.0.zip` | 自编静态库，产物指向 `D:\Programs\Qt\6.12.0-static`，见「编译静态 Qt」 |
| CMake | 3.16 及以上 | 可使用 Qt 安装包自带的 `Tools\CMake_64` |
| MinGW | **15.1.0** | `Tools\mingw1510_64`（已从 13.1.0 升级），动态/静态两条线共用同一套编译器 |

以下步骤假定 Qt 安装在 `D:\Programs\Qt`，如实际路径不同请自行替换；并假定 Qt 的 `bin`、MinGW 15.1.0 的 `bin`、`Tools\CMake_64\bin` 都已在**系统 `PATH`** 里。

---

## 动态构建

### 1. 打开命令行

普通命令提示符（`cmd`）即可，不用专门从 Qt 的开始菜单快捷方式启动——系统 `PATH` 里已经包含：

| 目录 | 提供 |
| --- | --- |
| `D:\Programs\Qt\6.12.0\mingw_64\bin` | Qt 的 DLL 与 `windeployqt` |
| `D:\Programs\Qt\Tools\mingw1510_64\bin` | `g++` / `mingw32-make`（15.1.0） |
| `D:\Programs\Qt\Tools\CMake_64\bin` | `cmake` |

> 两个 `构建*.bat` 内部已经把这三个目录**写死并临时前置**，双击运行不依赖系统 `PATH`；只有手工敲下面的命令时才需要 `PATH` 能找到它们。
> 改完环境变量后如果新开的 cmd 里仍提示「不是内部或外部命令」或找不到 `windeployqt`，通常是 `explorer.exe` 还在用旧环境——
> 注销重登、或重启资源管理器；直接双击 bat 不受影响。

### 2. 确认工具链

三条命令都要有版本输出，其中 `g++ --version` 必须是 **15.1.0**：

```bat
cmake --version
g++ --version
mingw32-make --version
```

> 换机器或重装 Qt 后若提示「不是内部或外部命令」，把上表三个目录按这个顺序补进系统 `PATH` 即可（Qt 的 `bin` 放最前）。

### 3. 配置并编译

```bat
cmake -B build-dynamic -G "MinGW Makefiles" ^
      -DCMAKE_PREFIX_PATH=D:\Programs\Qt\6.12.0\mingw_64 ^
      -DCMAKE_BUILD_TYPE=Release

cmake --build build-dynamic
```

参数说明：

- `-DCMAKE_PREFIX_PATH`：指向 Qt 的 **kit 目录**（`6.12.0\mingw_64`），不是 `Tools\` 下的目录，用于让 CMake 找到 `Qt6Config.cmake`。
- `-DCMAKE_BUILD_TYPE`：`MinGW Makefiles` 属于单配置生成器，构建类型必须在配置阶段指定。
- 构建目录叫 `build-dynamic`（动态构建用），**静态构建用 `build\`**，两套缓存互不覆盖。

编译产物：`build-dynamic\BGAssistant.exe`

### 4. 重新构建

若此前配置失败过，`build-dynamic` 目录中可能残留错误的 `CMakeCache.txt`，先清理再重新配置：

```bat
rmdir /s /q build-dynamic
```

---

## 部署（动态构建：打包为独立目录）

编译产出的 exe 依赖 Qt 的动态库，直接拷到没装 Qt 的机器上无法运行。用 `windeployqt` 把所需的 DLL 与插件复制到 exe 同级目录：

```bat
cd /d D:\Workspaces\BG_Assistant\build-dynamic
windeployqt --release --compiler-runtime BGAssistant.exe
```

参数说明：

- `--release`：复制 Release 版 Qt 库。若构建的是 Debug 版本，改用 `--debug`。
- `--compiler-runtime`：连同 MinGW 运行时（`libgcc_s_seh-1.dll`、`libstdc++-6.dll`、`libwinpthread-1.dll`）一并复制。缺少它们时，程序在干净的机器上会直接启动失败，且往往没有明确报错。

执行后 `build-dynamic\` 目录会补入：

```
BGAssistant.exe
Qt6Core.dll  Qt6Gui.dll  Qt6Widgets.dll
libgcc_s_seh-1.dll  libstdc++-6.dll  libwinpthread-1.dll
platforms\qwindows.dll
imageformats\   styles\   tls\   ...
```

整个 `build-dynamic\` 目录即可作为免安装的绿色包分发。

注意事项：

- **exe 文件名是 `BGAssistant.exe`（无下划线）**，由 `CMakeLists.txt` 中的 `project(BGAssistant ...)` 决定。若写成 `BG_Assistant.exe`，`windeployqt` 会报 `Cannot find file`。
- `windeployqt` 位于 Qt 的 `bin` 目录（`D:\Programs\Qt\6.12.0\mingw_64\bin`），系统 `PATH` 已包含该目录，普通 cmd 里可直接调用。
- **换过编译器后要清空 `release\` 重新 `windeployqt`**：`--compiler-runtime` 拷进来的运行时 DLL 必须和编译 exe 的 GCC 同版本（现在是 15.1.0）。目录里若残留旧的 13.1.0 版 `libstdc++-6.dll` / `libgcc_s_seh-1.dll`，会被优先加载，典型症状是启动报「找不到入口点」或直接闪退。
  > 重跑后建议比一下版本：`release\libstdc++-6.dll` 应与 `Tools\mingw1510_64\bin\libstdc++-6.dll` 一致（右键属性看「文件版本」）。若 windeployqt 仍拷了 13.1.0 的（它可能按 Qt 安装时的工具链目录去找），直接手动覆盖这三个 DLL 即可。
- 目录分发后位置变化，需重新勾选「开机自启」。

---

## 静态构建（单文件 exe）

动态构建的产物要带一堆 Qt DLL。想分发单个 exe，就走静态链接：先把 Qt 用 `-static` 编一份，之后 CMake 指向它，链出来的 exe 自带 Qt 与 MinGW 运行时，**不需要 `windeployqt`，也不需要任何 DLL**。

### 用法

1. 按后面「编译静态 Qt」把静态 Qt 6.12.0 装到 `D:\Programs\Qt\6.12.0-static`（**只做一次**）；
2. 双击 `构建static.bat`，等价于：

```bat
cmake -B build -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH=D:\Programs\Qt\6.12.0-static -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

产物：`build\BGAssistant.exe`，单文件直接分发，约 12～16 MB。

### 构建目录约定

| 目录 | 内容 | 由谁使用 |
| --- | --- | --- |
| `build\` | 静态构建缓存，**产物 `build\BGAssistant.exe` 就是单文件 exe** | `构建static.bat` |
| `build-dynamic\` | 动态构建缓存 | `构建release.bat` |
| `release\` | 动态构建的绿色包（含 DLL） | `构建release.bat` |

CMake 缓存与 Qt 的 prefix 绑定，两个 build 目录不要混用，否则每次切换都要全量重新配置。

### CMake 里的静态分支

`CMakeLists.txt` 通过 `Qt6::Core` 的 `TYPE` 判断是否为静态 Qt，静态时自动补两件事：

```cmake
get_target_property(_qt_core_lib_type Qt6::Core TYPE)
if(_qt_core_lib_type STREQUAL "STATIC_LIBRARY")
    qt_import_plugins(BGAssistant INCLUDE Qt6::QWindowsIntegrationPlugin)  # 平台插件不会被自动链入
    if(MINGW)
        target_link_options(BGAssistant PRIVATE -static)                   # 连 GCC 运行时一起静态链
    endif()
endif()
```

两处缺一不可：少了 `qt_import_plugins`，程序启动直接报 `could not find or load the Qt platform plugin "windows"`；少了 `-static`，exe 旁边仍会多出 `libstdc++-6.dll` / `libgcc_s_seh-1.dll` / `libwinpthread-1.dll`。

### 代价

- 静态 Qt **只有 Release 库**，用 gdb 调试时看不到 Qt 内部符号（自己的代码断点正常）。要步进 Qt 源码得再编一份 debug 静态库，又多 1～2 GB。
- 换 MinGW 版本必须重编 Qt（`-static` 会把 libstdc++ 静态链进 exe，两代运行时混在一起链接期就可能炸）。本项目已从 13.1.0 换到 **15.1.0**，之前用 13.1.0 编过的静态 Qt 必须删掉重编。
- 链接比动态慢几秒（要从静态库里抽取目标文件并做 dead-code 消除）。

---

## 编译静态 Qt

只需做一次。本例用 **Qt 6.12.0 源码包**（`qt-everywhere-src-6.12.0.zip`）+ **`Tools\mingw1510_64`（GCC 15.1.0）**，
保证静态 Qt 与后续编译 BGAssistant 用的是同一个编译器。

> 动态、静态两条线现在都是 **6.12.0**：动态用安装器装的官方预编译 kit（`6.12.0\mingw_64`），
> 静态是自己从源码编的那份（`D:\Programs\Qt\6.12.0-static`）。两条线共用同一个 MinGW 15.1.0，
> 但**不要混着用**：一个 exe 要么链动态 DLL，要么静态链，别交叉。

### 1. 解压源码

`qt-everywhere-src-6.12.0.zip` 解压出来就是源码根目录（`configure.bat` 就在第一层），例如：

```
D:\Programs\Qt\qt-everywhere-src-6.12.0\
├── configure.bat
├── qtbase\
├── qtdeclarative\
└── ...
```

> 如果用的是安装器里的 `Src` 目录（`D:\Programs\Qt\6.12.0\Src`），结构一样，把下面命令里的路径换掉即可。
> 注意源码版本要和 `-prefix` 装的静态 Qt 版本一致，别拿别的版本的 `Src` 编出个 6.12.0 的目录名。

### 2. 确认编译器

普通 cmd 即可（系统 `PATH` 已配好），先确认版本：

```bat
g++ --version
```

必须是 **15.1.0**。**Qt 和项目必须用同一套 GCC**：之前用 13.1.0 编过静态 Qt 的话，
先 `rmdir /s /q D:\Programs\Qt\6.12.0-static` 再重编，别混着用。

### 3. 配置（在单独的空构建目录里）

```bat
mkdir D:\Programs\Qt\build-static-6120
cd /d D:\Programs\Qt\build-static-6120

D:\Programs\Qt\qt-everywhere-src-6.12.0\configure.bat -static -static-runtime -release -opensource ^
  -confirm-license -nomake examples -nomake tests ^
  -skip qtwebengine -skip qtmultimedia -skip qt3d -skip qtcanvaspainter ^
  -skip qtcharts -skip qtgraphs -skip qtdatavis3d -skip qtlottie ^
  -skip qtvirtualkeyboard -skip qtlocation -skip qtwebview ^
  -skip qtquick3d -skip qtquick3dphysics -skip qtquicktimeline -skip qtquickeffectmaker ^
  -skip qttools -skip qtdoc -skip qttranslations -skip qtspeech ^
  -prefix D:\Programs\Qt\6.12.0-static
```

参数说明：

- `-static`：产出静态库（`.a`），这是单文件 exe 的前提。
- `-static-runtime`：Qt 自身也静态链接 MinGW 运行时。
- `-opensource -confirm-license`：开源版并跳过交互式许可确认。
- `-skip ...`：把用不到的模块排掉，编译时间从 1～3 小时降到 **30～45 分钟**。
- `-prefix`：安装目录，`构建static.bat` 里的 `QT_STATIC` 默认指向这里。

> **`-skip` 不会传递，跳过"被别人依赖"的模块会连带报错。** 上面两处是踩过的坑：
> 1. 不跳 `qtdeclarative`（Qt Quick 运行时）——跳了的话 `qtcanvaspainter`、`qtquick3dphysics`、`qtgraphs`
>    等模块会逐个报 `Module 'X' depends on 'qtdeclarative'`，而且 Qt 一次只报一个；
> 2. 跳过 `qttools` 就必须连 `qtdoc`、`qttranslations` 一起跳——后两者要 `qttools` 里的 `lupdate` / `lrelease`，
>    否则报 `Module 'qtdoc' depends on 'qttools'`；
> 3. `qtspeech` 依赖被跳过的 `qtmultimedia`，所以一并跳掉。
>
> 反过来说：跳过的模块必须是"没有启用中的模块依赖它"的叶子（图表、三维、多媒体、文档、翻译都属于此类）。

> 本项目只用 Widgets，编出来的 Qt Quick 静态库**不会被链进 exe**（静态链接只拉用到的符号），只是占点磁盘。

> 若 configure 报 `Unknown module ...`，说明那个模块在 6.12 里已经不存在（Qt 各版本模块有增删），
> 把对应的 `-skip` 删掉再跑一次即可，其余参数不用动。

> **configure 失败后重跑前，先把构建目录清空**（`rmdir /s /q D:\Programs\Qt\build-static-6120` 再 `mkdir`），
> 残留的 `CMakeCache.txt` 会把上一次的错误配置带进来。

> configure 会先自检 CMake / 编译器 / Python 版本。本机工具链已经是 MinGW 15.1.0，
> 若还报某项版本过低，按提示用 `MaintenanceTool` 补装即可；但**再动 MinGW 就意味着这套静态库
> 和动态构建都要重来**，升级前先想清楚。

### 4. 编译并安装

```bat
mingw32-make -j8
mingw32-make install
```

`-j8` 按 CPU 核心数调整。装完 `D:\Programs\Qt\6.12.0-static` 约 1～2 GB，中间目录 `build-static-6120` 可以直接删掉。

### 5. 验证

存在下面这个文件即成功，接着跑 `构建static.bat`：

```bat
dir D:\Programs\Qt\6.12.0-static\lib\cmake\Qt6\Qt6Config.cmake
```

### 注意

- 编译期间源码 + 中间文件约占 **20～30 GB**（6.12 的源码包比 6.9 大不少），别放在剩余空间紧张的盘。
- 静态 Qt 与动态 Qt 可共存，互不影响；但「静态 Qt 与编译器」必须配对，换编译器就得重编。
- 只编一次：之后改项目代码不会重编 Qt，除非换 Qt 版本、换编译器或改 `configure` 参数。
- 漏了 `-static-runtime`（Qt 侧）或 `-static`（项目侧），exe 仍会带出三个 MinGW 运行时 DLL。

### 常见错误

都是实际踩过的，按现象对照处理：

| 现象 | 原因 | 处理 |
| --- | --- | --- |
| `Module 'X' depends on 'Y', but building 'Y' was explicitly disabled` | `-skip` 不会传递，跳了被别人依赖的模块 | 按提示把 X 也加进 `-skip`；或干脆不跳 Y（详见上一节的三条记录） |
| `另一个程序正在使用此文件，进程无法访问` + 某个 `.rcc/qmlcache/*.cpp` 报 Error 1 | 杀软/索引/同步盘占着刚生成的文件 | 先 `mingw32-make -j4` 重试（多数是瞬时）；仍失败就删掉那个文件再跑；根治是把构建目录加进 Defender 排除项 |
| `cc1plus.exe: out of memory` / 编译进程直接消失 | `-j8` 并行太高，单 TU 峰值能到 2 GB | 降 `-j4` 或 `-j2` 续编 |
| `Unknown module ...` | 该模块在这一版 Qt 里不存在 | 删掉对应的 `-skip` |

**这几种失败都不用重新 `configure`**：改完参数直接 `mingw32-make` 续编，已完成的进度会保留。

---

## 源码目录结构

```
.
├── CMakeLists.txt      构建配置（含静态 Qt 分支）
├── app.rc.in           exe 图标资源模板（由 CMake 生成 app.rc）
├── 构建release.bat     动态构建（build-dynamic + windeployqt）
├── 构建static.bat      静态构建（build + 单文件 exe）
├── 更新git代码.bat     git pull 拉取更新
├── LICENSE             GPLv3 许可证全文
├── README.md           用户文档（功能与使用）
├── BUILD.md            本文
├── main.cpp            程序入口、托盘与菜单、锁屏 / 路由 / 端口转发 / Chrome 多开子进程分支
├── mainwindow.*        主窗口（预留，当前未显示）
├── configform.*        设置窗口（开机自启、锁屏常亮、锁屏密码）
├── lockscreen.*        神隐锁屏（全屏遮屏、盲打密码解锁）
├── routemanager.*      系统路由管理窗口（添加/删除路由 + 系统路由表弹窗）
├── routequery.*        读取 IPv4 路由表（IP Helper API + 长效路由注册表）
├── portproxy.*         端口转发的 netsh 封装（读列表、增删、IP Helper 服务状态）
├── portproxyform.*     端口转发窗口（规则列表 + 添加 / 更新 / 删除）
├── sandboxie.*         解析 Sandboxie.ini、路径映射、启动沙箱
├── sandboxmenu.*       托盘「沙箱」菜单（含「显示启动命令」窗口）
├── chromemultiopen.*   Chrome 多开的配置（ini）、数据/缓存目录、参数拼装与启动
├── chromemenu.*        托盘「系统 › Chrome多开」菜单
├── chromemultiopenform.* 多开管理窗口（列表、启动、数据位置、命令预览）
├── chromeprofileform.* 单个多开的新建 / 编辑对话框
├── ipaddressedit.*     仿系统 IP 地址控件的四段输入框
├── elevation.*         管理员提权（ShellExecuteEx runas）+ 系统程序绝对路径
├── aes.*               AES-ECB 加密（纯 C++ 实现，无外部依赖）
├── resources.qrc       图标资源清单
└── icons/              图标资源
```

---

## 实现说明

### 系统路由管理的提权方式

添加/删除路由需要管理员权限，采用「新开一个进程、在新进程里提权」：

1. 主进程（托盘）用 `TokenElevation` 判断自己是否已是管理员。
2. 不是 → `ShellExecuteEx` + `runas` 重新启动自身，参数 `RouteManager`，触发 UAC。
3. 新进程识别到 `RouteManager` 参数后只显示窗口、**不创建托盘图标**，关闭窗口即退出。
4. 主进程本身已提权时直接在当前进程弹窗，不再多开一个。
5. 用户点了 UAC 的「否」时静默返回。

### 系统路由管理与原版的差异

命令本身与原版相同：`route [-p] add <网段> mask <掩码> <网关> metric 1`、`route delete <网段>`（`-p` 按官方语法放在子命令前）。差异只在外围：

| 环节 | 原版 | 本版 |
| --- | --- | --- |
| 执行命令 | `cmd.exe /c route ...`，会闪控制台窗口 | 直接调用 `System32\route.exe`（绝对路径），`CREATE_NO_WINDOW` 隐藏窗口 |
| 结果校验 | `route print > %temp%\RoutePrint.txt` 再读文本 | `GetIpForwardTable2` 直接查路由表，不产生临时文件 |
| 显示路由表 | 导出 `%temp%\RoutePrint.txt`，用 Notepad++ 打开 | 原生表格窗口，可刷新 |
| 退出清理 | 关闭窗口时删除临时文件 | 没有临时文件 |

### 端口转发的实现

- 规则来自 `netsh interface portproxy show v4tov4`，不直接读注册表：`PortProxy\v4tov4\tcp` 下的值格式没有公开文档。
- 解析按「一行 4 个字段、第 2/4 个是 1-65535 的整数」筛数据行，不依赖本地化的表头（「地址 端口 地址 端口」）。`0.0.0.0` 在部分 Windows 版本会显示成 `*`，而 `*` 与 `0.0.0.0` 在 netsh 里是两条不同的表项：表单统一按 `0.0.0.0` 显示，原始写法单独留在 `storedListenAddress` 里，删除时原样回填。
- `netsh` 有 `set v4tov4`，但它只能改连接地址/端口（侦听地址 + 端口才是表项主键），并且存在「只写了配置、没让 IP Helper 重新绑定监听」的情况。所以「更新选中」一律按「删旧 + 加新」重建：
  - 侦听端点没变 → 先删后加（同一个表项只能存在一条，中间有极短断流）；
  - 侦听端点变了 → 先加新、再删旧，添加失败时旧规则仍然可用。
- 每次 add / delete 后都重新拉一次列表确认结果，不只看 netsh 的退出码；失败时把 netsh 原始输出和完整命令行一起显示，方便手动重试。
- 添加/修改前拦「自环」配置：侦听与连接完全相同，或者侦听 `0.0.0.0` 且同端口转到本机地址。后者用 `GetAdaptersAddresses`（`isLocalIpv4Address`，另含 127.0.0.0/8）判断目标是不是本机 IP——0.0.0.0 的监听覆盖所有网卡，转出去的连接会再次命中自己的监听，形成死循环。
- 窗口底部显示 IP Helper 服务（`iphlpsvc`）状态，用 `OpenSCManager` / `QueryServiceStatus`（advapi32）查询。服务没跑时规则能建、但不会监听，属于最常见的「加了不生效」。

### Chrome 多开的启动方式

多开不需要管理员权限，所以只借用了「新进程 + 参数」这套做法，不走 `runas`：

1. 托盘菜单里的「多开管理…」用 `QProcess::startDetached` 重启自身，参数 `ChromeMultiOpen`。
2. 新进程识别到该参数后只显示管理窗口、不创建托盘图标，关闭窗口即退出。
3. 点某个多开（或窗口里的「启动选中」）时直接用 `QProcess::startDetached` 拉起 `chrome.exe`。

数据目录由「数据根目录 + 名称」推导，不单独存档：改数据位置后所有多开都指向新位置，同名子目录直接沿用。
命令行按 `--user-data-dir=<根>/<名称> --disk-cache-dir=%TEMP%/<名称>` 起头，再按勾选项追加代理 / UA / `--app` / 窗口 / 显示模式等参数。
参数值本身不加引号（交给 `QProcess` 做进程级转义），只有界面上展示的命令行才补引号。

浏览器探测顺序：`%LOCALAPPDATA%\Google\Chrome\Application\chrome.exe`，再是 `%ProgramFiles%` / `%ProgramFiles(x86)%` / `%ProgramW6432%` 下的同名路径；都没找到时「Chrome多开」菜单整体隐藏。

### 外部命令一律用绝对路径

`CreateProcess` 解析相对名时按「应用程序自身目录 → 当前目录 → System32 → Windows 目录 → PATH」搜索，
在提权进程里用裸文件名（如 `route.exe`）等于把管理员权限交给同目录下的同名 exe，属于提权漏洞。

因此所有系统命令都通过 `elevation.*` 里的 `system32Path()` 取 `System32` 绝对路径（内部走 `GetSystemDirectoryW`，不写死盘符）。
没用 `%SystemRoot%` 是因为环境变量可被 `HKCU\Environment` 覆盖并被新进程继承。

### 沙箱菜单的解析细节

- `SandMan.exe` 依次在 `%ProgramW6432%`、`%ProgramFiles%`、`%ProgramFiles(x86)%` 下的 `Sandboxie-Plus\` / `Sandboxie\` 里查找，找不到时按默认路径启动并弹提示。
- ini 编码自动识别：带 BOM 的 UTF-16 LE/BE、无 BOM 的 UTF-16、UTF-8，最后回退到系统本地编码（简中为 GBK）。**Sandboxie 默认写的是 UTF-16 LE**，按 UTF-8 硬读会全是乱码。
- 启动用 `QProcess::startDetached` 传参数列表，路径里的空格不需要手工转义。
- 同一沙箱多条 `RunCommand` 时每条都是独立菜单项；完全相同的重复行只保留一条。
- 排序用 `std::stable_sort` + `Qt::CaseInsensitive`：仅大小写不同的名字保持 ini 里的先后顺序。

### 其他

- **托盘图标动画是有意设计**：太极图标以 40ms 间隔循环播放，常驻重绘属于预期行为。
- `icons\app.ico` 通过 `app.rc.in` 生成 `app.rc` 后编入 exe 资源段，这是 exe 图标生效的唯一途径（CMake 没有 `WINDOWS_ICON` 属性）。

---

## 许可证与再分发

本项目采用 **GPLv3**。原因：默认发布的是**静态链接 Qt 的单文件 exe**，属于分发二进制，GPLv3 是最省心的组合（另一条路是动态链接 + 满足 LGPLv3）。

- 分发 exe 时**同时提供本仓库源码**（或书面提供源码的途径），这是 GPLv3 的核心义务。
- 不得附加额外限制条款；修改后的版本同样以 GPLv3 发布。
- 若要以 LGPLv3 方式使用 Qt 而不公开源码，请改回动态构建，并遵守 LGPLv3 关于替换 Qt 库的要求。
