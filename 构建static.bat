@echo off
rem ============================================================
rem 静态构建：用静态编译的 Qt 6.12.0 产出单文件 exe（不依赖任何 Qt DLL），
rem           编译完成后用 tools\upx.exe 压缩，压缩产物放到 release\。
rem
rem 前置条件：先按 BUILD.md「编译静态 Qt」一节把静态 Qt 装到 QT_STATIC，
rem           没装过就把下面这行改成你的实际安装目录。
rem
rem 产物：release\BGAssistant.exe（UPX 压缩，文件名不变，单文件直接分发）
rem       build\BGAssistant.exe 保留未压缩的原件，方便排查问题
rem
rem 注意：UPX 压过的 exe 偶尔会被杀毒软件误报，分发前建议先过一遍杀软。
rem
rem 【编码】本文件必须保存为 GBK（ANSI）编码，且严禁加入 chcp 65001。
rem         原因：cmd 在读取批处理过程中切换代码页会丢失文件读取位置，
rem         导致后面的行被截断成乱码命令执行（表现为报 'PX' 不是内部命令之类）。
rem         若用 VS Code 编辑，请确认右下角显示 GBK 而不是 UTF-8。
rem ============================================================
setlocal

rem 以脚本所在目录为工作目录：从快捷方式或别处调用时也不会跑错地方
cd /d "%~dp0"

set "QT_STATIC=D:\Programs\Qt\6.12.0-static"
set "MINGW_BIN=D:\Programs\Qt\Tools\mingw1510_64\bin"
set "CMAKE_BIN=D:\Programs\Qt\Tools\CMake_64\bin"
rem 变量名不能用 UPX：UPX 自身会读取名为 UPX 的环境变量，值不是合法配置时
rem 直接报 "invalid string ... in environment variable 'UPX'" 并拒绝压缩。
rem 顺手把外部可能遗留的 UPX 变量清掉，避免继承来的值干扰本次压缩。
set "UPX="
set "UPX_EXE=%~dp0tools\upx.exe"

if not exist "%QT_STATIC%\lib\cmake\Qt6\Qt6Config.cmake" (
    echo [错误] 没找到静态 Qt：%QT_STATIC%
    echo.
    echo 请先按 BUILD.md「编译静态 Qt」一节生成静态 Qt 6.12.0，
    echo 或修改本文件里的 QT_STATIC 变量指向已有的静态 Qt 目录。
    pause
    exit /b 1
)

if not exist "%UPX_EXE%" (
    echo [错误] 没找到 UPX：%UPX_EXE%
    echo.
    echo 请把 upx.exe 放到本脚本同级的 tools\ 目录下。
    pause
    exit /b 1
)

rem 不依赖系统 PATH：MinGW 与 CMake 的目录临时前置，改完环境变量没重开资源管理器也能跑
set "PATH=%MINGW_BIN%;%CMAKE_BIN%;%PATH%"

rem 静态构建用 build\，动态构建用 build-dynamic\，两者互不干扰
rem release\ 每次整个重建，避免残留上一次的旧产物
rmdir /s /q build 2>nul
rmdir /s /q release 2>nul

rem 目录被占用（比如 build\BGAssistant.exe 还跑着）时 rmdir 会失败，直接停下来说清楚
if exist build goto :locked
if exist release goto :locked

cmake -B build -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH="%QT_STATIC%" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 goto :fail

cmake --build build
if errorlevel 1 goto :fail

if not exist build\BGAssistant.exe goto :fail

if not exist release mkdir release

rem -o 把压缩结果直接写到 release\：build\ 里的原件保持未压缩，
rem 出问题时可以直接拿它对照。静态链接的 exe 很大，--best 要跑一两分钟，别以为卡死了。
echo.
echo 正在用 UPX 压缩 build\BGAssistant.exe ...
pushd build
"%UPX_EXE%" --best -o "..\release\BGAssistant.exe" BGAssistant.exe
set "UPX_RESULT=%errorlevel%"
popd
if not "%UPX_RESULT%"=="0" goto :fail
if not exist "release\BGAssistant.exe" goto :fail

for %%A in (build\BGAssistant.exe) do set "RAW=%%~zA"
for %%A in (release\BGAssistant.exe) do set "PACKED=%%~zA"

echo.
echo 完成：release\BGAssistant.exe
echo       未压缩 %RAW% 字节  -^>  压缩后 %PACKED% 字节
pause
exit /b 0

:fail
echo.
echo [失败] 构建或压缩没有通过，请看上面的报错信息。
echo        若是 UPX 报错，常见原因：
echo          1. exe 还在托盘里运行（右键图标 - 退出）
echo          2. 被杀毒软件锁定或拦截
echo          3. 报 GUARD_CF 不支持，把命令加上 --force 重跑（会连带关掉 CFG）
echo        release\ 里可能是空的，别直接拿去分发。
pause
exit /b 1

:locked
echo.
echo [错误] build\ 或 release\ 删不掉，多半是有程序正在使用：
echo        1. 托盘里的 BGAssistant.exe 还没退出（右键图标 - 退出）
echo        2. 资源管理器 / 编辑器正打开该目录
echo        3. 杀毒软件正在扫描上一次生成的 exe
echo        处理完后重跑本脚本。
pause
exit /b 1
