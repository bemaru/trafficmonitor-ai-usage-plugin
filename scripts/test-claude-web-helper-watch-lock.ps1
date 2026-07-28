param()

$ErrorActionPreference = 'Stop'

function Assert-True {
    param(
        [bool]$Condition,
        [string]$Message
    )

    if (-not $Condition) {
        throw $Message
    }
}

function Write-WatchLock {
    param(
        [string]$Path,
        [int]$ProcessId,
        [DateTimeOffset]$StartedAt
    )

    $payload = [ordered]@{
        pid = $ProcessId
        mode = 'watch'
        started_at = $StartedAt.ToUniversalTime().ToString('o')
        refresh_ms = 60000
    }
    $json = $payload | ConvertTo-Json
    [System.IO.File]::WriteAllText($Path, $json, [System.Text.UTF8Encoding]::new($false))
}

$repoRoot = Split-Path $PSScriptRoot -Parent
$wrapperPath = Join-Path $repoRoot 'scripts\claude-web-helper.ps1'
$helperPath = Join-Path $repoRoot 'helper\claude-web-helper\index.mjs'
$testRoot = Join-Path ([System.IO.Path]::GetTempPath()) ('trafficmonitor-helper-lock-test-' + [guid]::NewGuid().ToString('N'))
$baseDir = Join-Path $testRoot 'trafficmonitor-claude-usage-plugin'
$watchLockPath = Join-Path $baseDir 'claude-web-helper-watch.lock'
$originalLocalAppData = $env:LOCALAPPDATA
$unrelatedProcess = $null
$watchProcess = $null
$dummyScriptPath = Join-Path $testRoot 'index.mjs'
$watchStdoutPath = Join-Path $testRoot 'watch-stdout.txt'
$watchStderrPath = Join-Path $testRoot 'watch-stderr.txt'

try {
    New-Item -ItemType Directory -Path $baseDir -Force | Out-Null
    $env:LOCALAPPDATA = $testRoot

    $nodePath = (Get-Command node -CommandType Application).Source
    [System.IO.File]::WriteAllText(
        $dummyScriptPath,
        'setInterval(() => {}, 60000);',
        [System.Text.UTF8Encoding]::new($false)
    )
    $unrelatedProcess = Start-Process -FilePath $nodePath `
        -ArgumentList $dummyScriptPath, 'watch' `
        -WindowStyle Hidden `
        -PassThru
    Start-Sleep -Milliseconds 500
    $unrelatedProcess.Refresh()
    Write-WatchLock `
        -Path $watchLockPath `
        -ProcessId $unrelatedProcess.Id `
        -StartedAt ([DateTimeOffset]$unrelatedProcess.StartTime.AddHours(-1))

    & $wrapperPath stop
    $unrelatedProcess.Refresh()
    Assert-True (-not $unrelatedProcess.HasExited) 'The wrapper stopped an unrelated process referenced by a stale lock.'
    Assert-True (-not (Test-Path $watchLockPath)) 'The wrapper did not remove the stale unrelated-process lock.'
    Write-Host 'PASS PowerShell stop preserves an unrelated process and removes its stale lock.'

    [System.IO.File]::WriteAllText(
        $watchLockPath,
        '{not-json',
        [System.Text.UTF8Encoding]::new($false)
    )
    & $wrapperPath stop
    $unrelatedProcess.Refresh()
    Assert-True (-not $unrelatedProcess.HasExited) 'The wrapper stopped an unrelated process while handling an unreadable lock.'
    Assert-True (-not (Test-Path $watchLockPath)) 'The wrapper did not remove the unreadable watch lock.'
    Write-Host 'PASS PowerShell stop removes an unreadable lock without terminating another process.'

    Write-WatchLock `
        -Path $watchLockPath `
        -ProcessId $unrelatedProcess.Id `
        -StartedAt ([DateTimeOffset]$unrelatedProcess.StartTime.AddHours(-1))

    $watchProcess = Start-Process -FilePath $nodePath `
        -ArgumentList '--disable-warning=ExperimentalWarning', $helperPath, 'watch' `
        -WindowStyle Hidden `
        -RedirectStandardOutput $watchStdoutPath `
        -RedirectStandardError $watchStderrPath `
        -PassThru
    for ($attempt = 0; $attempt -lt 20; $attempt++) {
        Start-Sleep -Milliseconds 500
        $currentLock = Get-Content $watchLockPath -Raw -ErrorAction SilentlyContinue | ConvertFrom-Json
        if ($currentLock -and [int]$currentLock.pid -eq $watchProcess.Id) {
            break
        }
        $watchProcess.Refresh()
        if ($watchProcess.HasExited) {
            break
        }
    }
    $watchProcess.Refresh()
    Assert-True (-not $watchProcess.HasExited) 'The Node watcher did not start after encountering a reused-PID lock.'

    $activeLock = Get-Content $watchLockPath -Raw | ConvertFrom-Json
    Assert-True `
        ([int]$activeLock.pid -eq $watchProcess.Id) `
        "The Node watcher did not replace the reused-PID lock (watch $($watchProcess.Id), unrelated $($unrelatedProcess.Id), lock $($activeLock.pid), stdout: $(Get-Content $watchStdoutPath -Raw -ErrorAction SilentlyContinue), stderr: $(Get-Content $watchStderrPath -Raw -ErrorAction SilentlyContinue))."
    Write-Host 'PASS Node lock acquisition replaces a lock owned by an unrelated process.'

    & $wrapperPath status
    Assert-True (Test-Path $watchLockPath) 'The wrapper removed the valid helper watcher lock as stale.'

    & $wrapperPath stop
    Start-Sleep -Milliseconds 500
    $watchProcess.Refresh()
    Assert-True $watchProcess.HasExited 'The wrapper did not stop the validated helper watcher.'
    Assert-True (-not (Test-Path $watchLockPath)) 'The wrapper did not remove the valid watcher lock after stop.'
    Write-Host 'PASS PowerShell status and stop recognize only the validated helper watcher.'
}
finally {
    if ($watchProcess) {
        $watchProcess.Refresh()
        if (-not $watchProcess.HasExited) {
            Stop-Process -Id $watchProcess.Id -Force -ErrorAction SilentlyContinue
        }
    }
    if ($unrelatedProcess) {
        $unrelatedProcess.Refresh()
        if (-not $unrelatedProcess.HasExited) {
            Stop-Process -Id $unrelatedProcess.Id -Force -ErrorAction SilentlyContinue
        }
    }
    $env:LOCALAPPDATA = $originalLocalAppData

    $resolvedTempRoot = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
    $resolvedTestRoot = [System.IO.Path]::GetFullPath($testRoot)
    if ($resolvedTestRoot.StartsWith($resolvedTempRoot, [System.StringComparison]::OrdinalIgnoreCase) -and
        (Test-Path -LiteralPath $resolvedTestRoot)) {
        Remove-Item -LiteralPath $resolvedTestRoot -Recurse -Force
    }
}

Write-Host 'All Claude web helper watch-lock tests passed.'
