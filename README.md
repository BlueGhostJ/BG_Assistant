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
| Qt（动态构建） | 6.9.0（MinGW 64-bit） | 安装器装的 kit，`build-dynamic\` 用它 |
| Qt 源码（静态构建） | `qt-everywhere-src-6.12.0.zip` | 自编静态库，产物指向 `D:\Qt\6.12.0-static`，见「编译静态 Qt」 |
| CMake | 3.16 及以上 | 可使用 Qt 安装包自带的 `Tools\CMake_64` |
| MinGW | 13.1.0（随 Qt 安装） | 位于 `Tools\mingw1310_64`，动态/静态两条线共用 |

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
cmake -B build-dynamic -G "MinGW Makefiles" ^
      -DCMAKE_PREFIX_PATH=D:\Programs\Qt\6.9.0\mingw_64 ^
      -DCMAKE_BUILD_TYPE=Release

cmake --build build-dynamic
```

参数说明：

- `-DCMAKE_PREFIX_PATH`：指向 Qt 的 **kit 目录**（`6.9.0\mingw_64`），不是 `Tools\` 下的目录，用于让 CMake 找到 `Qt6Config.cmake`。
- `-DCMAKE_BUILD_TYPE`：`MinGW Makefiles` 属于单配置生成器，构建类型必须在配置阶段指定。
- 构建目录叫 `build-dynamic`（动态构建用），**静态构建用 `build\`**，两套缓存互不覆盖，详见「静态构建」一节。

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
- `windeployqt` 位于 Qt 的 `bin` 目录（`D:\Programs\Qt\6.9.0\mingw_64\bin`），Qt 命令行快捷方式已将其加入 `PATH`。
- 目录分发后位置变化，需重新勾选「开机自启」，原因见下节。

---

## 静态构建（单文件 exe）

动态构建的产物要带一堆 Qt DLL。想分发单个 exe，就走静态链接：先把 Qt 用 `-static` 编一份，之后 CMake 指向它，链出来的 exe 自带 Qt 与 MinGW 运行时，**不需要 `windeployqt`，也不需要任何 DLL**。

### 用法

1. 按下一节「编译静态 Qt」把静态 Qt 6.12.0 装到 `D:\Qt\6.12.0-static`（**只做一次**）；
2. 双击 `构建static.bat`，等价于：

```bat
cmake -B build -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH=D:\Qt\6.12.0-static -DCMAKE_BUILD_TYPE=Release
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
- 换 MinGW 版本必须重编 Qt（ABI 变了）。
- 链接比动态慢几秒（要从静态库里抽取目标文件并做 dead-code 消除）。

---

## 编译静态 Qt

只需做一次。本例用 **Qt 6.12.0 源码包**（`qt-everywhere-src-6.12.0.zip`）+ **Qt 自带的 MinGW 13.1.0**，
保证静态 Qt 与后续编译 BGAssistant 用的是同一个编译器。

> 静态这条线用 6.12.0，动态那条线仍是安装器装的 6.9.0 kit，两套可以共存、互不干扰：
> `build-dynamic\` 用 6.9.0，`build\`（静态）用 6.12.0。

### 1. 解压源码

`qt-everywhere-src-6.12.0.zip` 解压出来就是源码根目录（`configure.bat` 就在第一层），例如：

```
D:\Programs\Qt\qt-everywhere-src-6.12.0\
├── configure.bat
├── qtbase\
├── qtdeclarative\
└── ...
```

> 如果用的是安装器里的 `Src` 目录（`D:\Programs\Qt\6.9.0\Src`），结构一样，把下面命令里的路径换掉即可。
> 注意源码版本要和 `-prefix` 装的静态 Qt 版本一致，别拿 6.9.0 的 `Src` 编出个 6.12.0 的目录名。

### 2. 打开 Qt 命令行并补 PATH

用开始菜单的 **「Qt 6.9.0 (MinGW 13.1.0 64-bit)」** 快捷方式打开 cmd，再执行：

```bat
for /d %i in ("D:\Programs\Qt\Tools\mingw*") do @set "PATH=%~i\bin;%PATH%"
set "PATH=D:\Programs\Qt\Tools\CMake_64\bin;%PATH%"
```

### 3. 配置（在单独的空构建目录里）

```bat
mkdir D:\Qt\build-static-6120
cd /d D:\Qt\build-static-6120

D:\Programs\Qt\qt-everywhere-src-6.12.0\configure.bat -static -static-runtime -release -opensource ^
  -confirm-license -nomake examples -nomake tests ^
  -skip qtwebengine -skip qtdeclarative -skip qtmultimedia -skip qt3d ^
  -skip qtcharts -skip qttools -skip qtquick3d -skip qtquicktimeline ^
  -prefix D:\Qt\6.12.0-static
```

参数说明：

- `-static`：产出静态库（`.a`），这是单文件 exe 的前提。
- `-static-runtime`：Qt 自身也静态链接 MinGW 运行时。
- `-opensource -confirm-license`：开源版并跳过交互式许可确认。
- `-skip ...`：本项目只用 Widgets（`qtbase`），把用不到的大模块排掉，编译时间从 1～3 小时降到 **20～40 分钟**。
- `-prefix`：安装目录，`构建static.bat` 里的 `QT_STATIC` 默认指向这里。

> 若 configure 报 `Unknown module ...`，说明那个模块在 6.12 里已经不存在（Qt 各版本模块有增删），
> 把对应的 `-skip` 删掉再跑一次即可，其余参数不用动。

> configure 会先自检 CMake / 编译器 / Python 版本。6.12 的要求可能高于 6.9 自带的工具链，
> 报「版本过低」时按提示用 `MaintenanceTool` 升级对应工具（**优先只升 CMake / Python，别动 MinGW**，
> 换了 MinGW 就得连带重编动态那套 6.9.0 的代码）。

### 4. 编译并安装

```bat
mingw32-make -j8
mingw32-make install
```

`-j8` 按 CPU 核心数调整。装完 `D:\Qt\6.12.0-static` 约 1～2 GB，中间目录 `build-static-6120` 可以直接删掉。

### 5. 验证

存在下面这个文件即成功，接着跑 `构建static.bat`：

```bat
dir D:\Qt\6.12.0-static\lib\cmake\Qt6\Qt6Config.cmake
```

### 注意

- 编译期间源码 + 中间文件约占 **20～30 GB**（6.12 的源码包比 6.9 大不少），别放在剩余空间紧张的盘。
- 静态 Qt 与动态 Qt 可共存，互不影响；但「静态 Qt 与编译器」必须配对，换编译器就得重编。
- 只编一次：之后改项目代码不会重编 Qt，除非换 Qt 版本、换编译器或改 `configure` 参数。
- 漏了 `-static-runtime`（Qt 侧）或 `-static`（项目侧），exe 仍会带出三个 MinGW 运行时 DLL。
- Qt 6.12 比 6.9 新，若本项目代码在 6.12 下有 API 变动导致编译报错，属于版本差异，改代码即可（动态 6.9 那条线不受影响）。

---

## 运行

直接运行 `build-dynamic\BGAssistant.exe`（动态构建）或 `build\BGAssistant.exe`（静态单文件），程序常驻系统托盘（太极图标）。

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

**注意**：写入的是当前运行中 exe 的绝对路径。若从 `build-dynamic\` 目录勾选，之后移动或删除该目录，自启项会失效，需重新勾选。

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
├── CMakeLists.txt      构建配置（含静态 Qt 分支）
├── app.rc.in           exe 图标资源模板（由 CMake 生成 app.rc）
├── 构建release.bat     动态构建（build-dynamic + windeployqt）
├── 构建static.bat      静态构建（build + 单文件 exe）
├── 更新git代码.bat     git pull 拉取更新
├── LICENSE             GPLv3 许可证全文
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

---

## 许可证

本项目采用 **GNU General Public License v3.0（GPLv3）** 发布，全文见仓库根目录的 [LICENSE](LICENSE)。

```
BG_Assistant
Copyright (C) 2026 BlueGhostJ

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
```

### 为什么是 GPLv3

- 本项目基于 **Qt 6**，其开源版以 GPLv3 / LGPLv3 双许可提供。**静态构建（`构建static.bat`）把 Qt 直接链进 exe**，属于分发二进制，选 GPLv3 是与之最省心的组合（另一条路是动态链接 + 满足 LGPLv3）。
- 若你打算以 LGPLv3 方式使用 Qt 而不公开源码，请改回动态构建，并遵守 LGPLv3 关于替换 Qt 库的要求。

### 再分发时注意

- 分发 exe 时**同时提供本仓库源码**（或书面提供源码的途径），这是 GPLv3 的核心义务。
- 不得附加额外限制条款；修改后的版本同样以 GPLv3 发布。
- 若从他人处获得修改版，对方同样受 GPLv3 约束。
