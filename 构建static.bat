@echo off
rem ============================================================
rem 静态构建：用静态编译的 Qt 产出单文件 exe（不依赖任何 Qt DLL）
rem
rem 前置条件：先按 README「编译静态 Qt」一节把静态 Qt 装到 QT_STATIC，
rem           没装过就把下面这行改成你的实际安装目录。
rem
rem 本文件是 UTF-8 编码，下面这句让控制台按 UTF-8 输出，中文才不会乱码。
rem ============================================================
chcp 65001 >nul
setlocal

set "QT_STATIC=D:\Qt\6.9.0-static"

if not exist "%QT_STATIC%\lib\cmake\Qt6\Qt6Config.cmake" (
    echo [错误] 没找到静态 Qt：%QT_STATIC%
    echo.
    echo 请先按 README「编译静态 Qt」一节生成静态 Qt，
    echo 或修改本文件里的 QT_STATIC 变量指向已有的静态 Qt 目录。
    pause
    exit /b 1
)

rem 静态构建用 build\，动态构建用 build-dynamic\，两者互不干扰
rmdir /s /q build 2>nul
rmdir /s /q release-static 2>nul

cmake -B build -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH="%QT_STATIC%" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 goto :fail

cmake --build build
if errorlevel 1 goto :fail

if not exist release-static mkdir release-static
copy build\BGAssistant.exe release-static\ >nul
echo.
echo 完成：release-static\BGAssistant.exe（静态链接，单文件，无需任何 DLL）
pause
exit /b 0

:fail
echo.
echo [失败] 构建没有通过，请看上面的报错信息。
pause
exit /b 1
