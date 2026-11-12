@echo off
rem 动态构建：产物需要带一堆 Qt DLL（windeployqt 补齐）。
rem 用 build-dynamic\ 而不是 build\，避免和静态构建（构建static.bat）的缓存互相覆盖。
rmdir /s /q build-dynamic
rmdir /s /q release
cmake -B build-dynamic -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build-dynamic
if not exist release mkdir release
copy build-dynamic\BGAssistant.exe release\
cd /d release
windeployqt --release --compiler-runtime BGAssistant.exe
pause
