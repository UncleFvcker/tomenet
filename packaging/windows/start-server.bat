@echo off
setlocal
cd /d "%~dp0"
set "TOMENET_PATH=%~dp0lib"
"%~dp0tomenet.server.exe"
echo.
echo Server has exited. Check lib\data\tomenet.log for details.
pause
