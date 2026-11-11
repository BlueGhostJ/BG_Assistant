@echo off
rmdir /s /q build
rmdir /s /q release
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
if not exist release mkdir release
copy build\BGAssistant.exe release\
cd /d release
windeployqt --release --compiler-runtime BGAssistant.exe
