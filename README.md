# BG_Assistant

Windows 桌面助手（Qt 6 / MinGW）。常驻系统托盘，提供开机自启、系统锁定与全屏遮屏。

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
| 设置 | 打开设置窗口，可开关「开机自启」 |
| 锁定 | 调用系统 `LockWorkStation` 锁定 Windows |
| 遮屏 | 启动全屏遮罩窗口，需输入密码解除 |
| 退出 | 结束程序 |

### 开机自启

勾选后写入注册表：

```
HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run
```

**注意**：写入的是当前运行中 exe 的绝对路径。若从 `build\` 目录勾选，之后移动或删除该目录，自启项会失效，需重新勾选。

---

## 注意事项

- **遮屏不是安全锁。** 它用于防止他人误操作或窥屏，无法阻止任务管理器结束进程、`Ctrl+Alt+Del`、`Win+L` 等系统级操作；多显示器环境下目前仅覆盖主屏。
- 遮屏密码当前硬编码在 `main.cpp` 中，仅适合个人临时使用，请勿用于真实的安全防护场景。
- **托盘图标动画持续播放是有意设计。** 太极图标以 40ms 间隔循环播放（`main.cpp` 中的 `QTimer`），常驻重绘属于预期行为，不作为待优化项，代码审查时无需再提出。
- `icons\app.ico` 通过 `app.rc.in` 生成 `app.rc` 后编入 exe 的资源段，这是 exe 图标生效的唯一途径（CMake 没有 `WINDOWS_ICON` 属性）。

---

## 目录结构

```
.
├── CMakeLists.txt      构建配置
├── app.rc.in           exe 图标资源模板（由 CMake 生成 app.rc）
├── main.cpp            程序入口、托盘与菜单
├── mainwindow.*        主窗口（预留，当前未显示）
├── configform.*        设置窗口（开机自启）
├── lockscreen.*        全屏遮屏
├── resources.qrc       图标资源清单
└── icons/              图标资源
```
