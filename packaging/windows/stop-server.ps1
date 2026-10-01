param([string]$ServerRoot = $PSScriptRoot)
$ErrorActionPreference = 'Stop'
$config = [IO.File]::ReadAllText((Join-Path $ServerRoot 'lib\config\tomenet.cfg'))
$passwordMatch = [regex]::Match($config, '(?m)^\s*CONSOLE_PASSWORD\s*=\s*"([^"\r\n]+)"\s*$')
$portMatch = [regex]::Match($config, '(?m)^\s*CONSOLE_PORT\s*=\s*(\d+)\s*$')
if (-not $passwordMatch.Success -or -not $portMatch.Success) {
    throw 'CONSOLE_PASSWORD or CONSOLE_PORT is missing from tomenet.cfg.'
}
$expectedExecutable = Get-Item -LiteralPath (Join-Path $ServerRoot 'tomenet.server.exe')
$matchingProcesses = @(Get-Process -Name 'tomenet.server' -ErrorAction SilentlyContinue | Where-Object {
    $_.Path -and (Get-Item -LiteralPath $_.Path).FullName -eq $expectedExecutable.FullName
})
if ($matchingProcesses.Count -ne 1) {
    throw 'Expected exactly one running server from this directory. Check its path and permissions.'
}
$taskServerProcess = $matchingProcesses[0]
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
