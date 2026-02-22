@echo off
xcopy language build\mingw\Debug\language\ /E /I /Y
xcopy data\fonts build\mingw\Debug\data\fonts\ /E /I /Y
cmake --preset mingw
cmake --build --preset mingw-debug --target EndlessSky
