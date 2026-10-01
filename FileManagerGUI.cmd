@echo off
setlocal
if exist "%~dp0build\Release\FileManagerGui.exe" (
    start "" "%~dp0build\Release\FileManagerGui.exe"
    exit /b
)
if exist "%~dp0build\Debug\FileManagerGui.exe" (
    start "" "%~dp0build\Debug\FileManagerGui.exe"
    exit /b
)
echo Please build the GUI first:
echo cmake -S . -B build
echo cmake --build build --config Release
pause
