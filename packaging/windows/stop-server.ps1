param([string]$ServerRoot = $PSScriptRoot)
$ErrorActionPreference = 'Stop'
$config = [IO.File]::ReadAllText((Join-Path $ServerRoot 'lib\config\tomenet.cfg'))
$passwordMatch = [regex]::Match($config, '(?m)^\s*CONSOLE_PASSWORD\s*=\s*"([^"\r\n]+)"\s*$')
$portMatch = [regex]::Match($config, '(?m)^\s*CONSOLE_PORT\s*=\s*(\d+)\s*$')
if (-not $passwordMatch.Success -or -not $portMatch.Success) {
    throw 'CONSOLE_PASSWORD or CONSOLE_PORT is missing from tomenet.cfg.'
}
$taskServerId = [int][IO.File]::ReadAllText((Join-Path $ServerRoot 'lib\data\tomenet.pid')).Trim()
$taskServerProcess = Get-Process -Id $taskServerId -ErrorAction Stop
$expectedExecutable = Get-Item -LiteralPath (Join-Path $ServerRoot 'tomenet.server.exe')
if ((Get-Item -LiteralPath $taskServerProcess.Path).FullName -ne $expectedExecutable.FullName) {
    throw 'The recorded PID belongs to a different executable.'
}
$client = New-Object Net.Sockets.TcpClient
try {
    $client.Connect('127.0.0.1', [int]$portMatch.Groups[1].Value)
    $stream = $client.GetStream()
    $stream.ReadTimeout = 5000
    $stream.WriteTimeout = 5000
    # Native console protocol: zero-terminated password followed by SHUTDOWN (16).
    $command = [Text.Encoding]::ASCII.GetBytes($passwordMatch.Groups[1].Value) + [byte[]](0, 16)
    $stream.Write($command, 0, $command.Length)
    if ($stream.ReadByte() -ne 16) { throw 'The server rejected the shutdown command.' }
} finally {
    $client.Dispose()
}
if (-not $taskServerProcess.WaitForExit(60000)) {
    throw 'Shutdown requested, but the server has not exited. Do not back up yet.'
}
Write-Host 'Server exited after saving. It is now safe to back up or migrate.'
