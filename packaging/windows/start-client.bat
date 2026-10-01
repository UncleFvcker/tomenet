@echo off
setlocal
cd /d "%~dp0"
set "serverAddress=127.0.0.1"
set /p "serverAddress=Server IP or hostname [127.0.0.1]: "
set "serverPort=18348"
set /p "serverPort=Game port [18348]: "
powershell.exe -NoProfile -Command "$address=$env:serverAddress; $port=0; if ($address -notmatch '^[a-zA-Z0-9._:-]{1,253}$' -or -not [int]::TryParse($env:serverPort,[ref]$port) -or $port -lt 1 -or $port -gt 65535) { Write-Error 'Invalid server address or port'; exit 1 }; Start-Process -FilePath (Join-Path (Get-Location) 'TomeNET.exe') -ArgumentList @('-p'+$port,$address) -WorkingDirectory (Get-Location)"
if errorlevel 1 pause
