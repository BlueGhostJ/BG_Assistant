@echo off
rem ============================================================
rem 动态构建：产物带一堆 Qt DLL（windeployqt 补齐），整个 release\ 目录一起分发
rem
rem 用 build-dynamic\ 而不是 build\，避免和静态构建（构建static.bat）的缓存互相覆盖。
rem
rem 脚本不依赖系统 PATH：Qt / MinGW / CMake 三个目录都在下面写死并临时前置，
rem 所以改完环境变量、甚至没重开资源管理器，双击也能直接跑。
rem 要换 Qt 版本，只改这三个变量即可。
rem
rem 本文件是 UTF-8 编码，下面这句让控制台按 UTF-8 输出，中文才不会乱码。
rem ============================================================
chcp 65001 >nul
setlocal

set "QT_KIT=D:\Programs\Qt\6.12.0\mingw_64"
set "MINGW_BIN=D:\Programs\Qt\Tools\mingw1510_64\bin"
set "CMAKE_BIN=D:\Programs\Qt\Tools\CMake_64\bin"

if not exist "%QT_KIT%\lib\cmake\Qt6\Qt6Config.cmake" (
    echo [错误] 没找到 Qt kit：%QT_KIT%
    echo.
    echo 请确认 Qt 6.12.0 已安装，或修改本文件顶部的 QT_KIT 变量。
    pause
    exit /b 1
)

set "PATH=%QT_KIT%\bin;%MINGW_BIN%;%CMAKE_BIN%;%PATH%"

rmdir /s /q build-dynamic 2>nul
rmdir /s /q release 2>nul

cmake -B build-dynamic -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH="%QT_KIT%" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 goto :fail

cmake --build build-dynamic
if errorlevel 1 goto :fail

if not exist release mkdir release
copy build-dynamic\BGAssistant.exe release\ >nul
if not exist "release\BGAssistant.exe" goto :fail

rem 用 pushd 进 release\：目录不存在时 cd 会静默失败，windeployqt 就会跑错地方
pushd release
windeployqt --release --compiler-runtime BGAssistant.exe
set "DEPLOY_RESULT=%errorlevel%"
popd
if not "%DEPLOY_RESULT%"=="0" goto :fail

echo.
echo 完成：release\BGAssistant.exe（含 Qt DLL，整个 release 目录一起分发）
pause
exit /b 0

:fail
echo.
echo [失败] 构建没有通过，请看上面的报错信息。
pause
exit /b 1
