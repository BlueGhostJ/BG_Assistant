@echo off
rem 动态构建：产物需要带一堆 Qt DLL（windeployqt 补齐）。
rem 用 build-dynamic\ 而不是 build\，避免和静态构建（构建static.bat）的缓存互相覆盖。
rem 前置条件：PATH 里有 Qt 的 bin 和 MinGW 15.1.0（Tools\mingw1510_64\bin）。
rem windeployqt --compiler-runtime 是按 PATH 找运行时 DLL 的，编译器换过就要清空 release\ 重跑。
rmdir /s /q build-dynamic
rmdir /s /q release
cmake -B build-dynamic -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build-dynamic
if not exist release mkdir release
copy build-dynamic\BGAssistant.exe release\
cd /d release
windeployqt --release --compiler-runtime BGAssistant.exe
pause
