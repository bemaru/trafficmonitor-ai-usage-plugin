param(
    [ValidateSet('login', 'once', 'watch', 'start', 'status', 'stop', 'reset')]
    [string]$Mode = 'status'
)

$ErrorActionPreference = 'Stop'
$helperDirCandidates = @(
    (Join-Path $PSScriptRoot 'helper\codex-web-helper'),
    (Join-Path (Split-Path $PSScriptRoot -Parent) 'helper\codex-web-helper')
)
$helperDir = $helperDirCandidates |
    Where-Object { Test-Path -LiteralPath (Join-Path $_ 'package.json') } |
    Select-Object -First 1
$helperDir = if ($helperDir) { (Resolve-Path -LiteralPath $helperDir).Path } else { $null }
$baseDir = if ($env:LOCALAPPDATA) {
    Join-Path $env:LOCALAPPDATA 'trafficmonitor-claude-usage-plugin'
} else {
    Join-Path $env:USERPROFILE '.cache\trafficmonitor-claude-usage-plugin'
}
$profileDir = Join-Path $baseDir 'codex-browser-profile'
$usagePath = Join-Path $baseDir 'codex-web-usage.json'
$statusPath = Join-Path $baseDir 'codex-web-helper-status.json'
$watchLockPath = Join-Path $baseDir 'codex-web-helper-watch.lock'
$stopPath = Join-Path $baseDir 'codex-web-helper-stop.json'

if (-not $helperDir) {
    throw 'Codex helper package.json not found next to the helper script or in the source helper directory.'
}
$helperScriptPath = Join-Path $helperDir 'index.mjs'

function Read-JsonFile {
    param([string]$Path)
    try {
        if (Test-Path -LiteralPath $Path) {
            return Get-Content -LiteralPath $Path -Raw -Encoding UTF8 | ConvertFrom-Json
        }
    } catch { }
    return $null
}

function Get-RelativeAgeText {
    param([object]$Timestamp)
    try {
        $span = [datetime]::UtcNow - ([datetime]$Timestamp).ToUniversalTime()
        if ($span.TotalSeconds -lt 0) { return 'just now' }
        if ($span.TotalMinutes -lt 1) { return ('{0:n0}s ago' -f $span.TotalSeconds) }
        if ($span.TotalHours -lt 1) { return ('{0:n0}m ago' -f $span.TotalMinutes) }
        return ('{0:n1}h ago' -f $span.TotalHours)
    } catch { return 'unknown' }
}

function Format-FileStatus {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) { return "$Path [missing]" }
    $item = Get-Item -LiteralPath $Path
    return "$Path [$([int64]$item.Length) bytes, $(Get-RelativeAgeText $item.LastWriteTime)]"
}

function Test-HelperWatchProcess {
    param([object]$WatchLock)
    $watchPid = 0
    if (-not $WatchLock -or [string]$WatchLock.mode -ne 'watch' -or
        -not [int]::TryParse([string]$WatchLock.pid, [ref]$watchPid) -or $watchPid -le 0) {
        return $false
    }
    try {
        $lockedScriptPath = [string]$WatchLock.script_path
        if (-not [IO.Path]::IsPathRooted($lockedScriptPath) -or
            $lockedScriptPath -notmatch '(?i)[\\/]codex-web-helper[\\/]index\.mjs$') { return $false }
        $process = Get-CimInstance Win32_Process -Filter "ProcessId = $watchPid" -ErrorAction Stop
        if (-not $process -or $process.Name -ne 'node.exe') { return $false }
        $commandLine = [string]$process.CommandLine
        $scriptPattern = '(?i)(^|\s)("' + [regex]::Escape($lockedScriptPath) + '"|' + [regex]::Escape($lockedScriptPath) + ')(?=\s|$)'
        if ($commandLine -notmatch $scriptPattern -or
            $commandLine -notmatch '(?i)(^|\s)watch(\s|$)') { return $false }
        $lockStartedAt = [DateTimeOffset]::Parse([string]$WatchLock.started_at).ToUniversalTime()
        $processStartedAt = ([DateTimeOffset]$process.CreationDate).ToUniversalTime()
        return [Math]::Abs(($lockStartedAt - $processStartedAt).TotalSeconds) -le 30
    } catch {
        # Fail closed to avoid stopping an unrelated process after PID reuse.
        return $false
    }
}

function Show-Status {
    Write-Host "Helper dir: $helperDir"
    Write-Host "Profile dir: $profileDir"
    Write-Host (Format-FileStatus $usagePath)
    Write-Host (Format-FileStatus $statusPath)
    $watchLock = Read-JsonFile $watchLockPath
    if (Test-HelperWatchProcess $watchLock) {
        Write-Host ("Watch: running (PID {0}, refresh_ms=60000)" -f [int]$watchLock.pid)
    } elseif ($watchLock) {
        Write-Host 'Watch: stopped or process identity could not be verified'
    } else {
        Write-Host 'Watch: none'
    }
    $status = Read-JsonFile $statusPath
    if ($status) {
        # Never print arbitrary file fields, exceptions, or HTTP response bodies.
        $state = [string]$status.state
        if ($state -match '^[a-z_]{1,64}$') { Write-Host "Status: $state" }
        try {
            $updated = [DateTimeOffset]::Parse([string]$status.updated_at).ToUniversalTime().ToString('o')
            Write-Host "Updated: $updated"
        } catch { }
    }
    $usage = Read-JsonFile $usagePath
    if ($usage) {
        try {
            $age = ([DateTimeOffset]::UtcNow - [DateTimeOffset]::Parse([string]$usage.updated_at)).TotalSeconds
            $valid = $usage.schema_version -eq 1 -and $usage.source -eq 'codex-web' -and $age -ge -30 -and $age -le 180
            foreach ($window in @(@{ Key = 'five_hour'; Seconds = 18000 }, @{ Key = 'seven_day'; Seconds = 604800 })) {
                $metric = $usage.($window.Key)
                if ($null -eq $metric -or $null -eq $metric.utilization -or
                    $metric.utilization -is [string] -or $metric.utilization -is [bool]) { $valid = $false; continue }
                $percent = [double]$metric.utilization
                $reset = [DateTimeOffset]::Parse([string]$metric.resets_at)
                $resetAge = ($reset - [DateTimeOffset]::UtcNow).TotalSeconds
                if ([double]::IsNaN($percent) -or [double]::IsInfinity($percent) -or
                    $percent -lt 0 -or $percent -gt 100 -or $resetAge -le 0 -or
                    $resetAge -gt ($window.Seconds + 60)) { $valid = $false }
            }
            Write-Host ("Snapshot fresh: {0}" -f $valid)
        } catch { Write-Host 'Snapshot fresh: False' }
    }
}

function Stop-HelperWatch {
    $watchLock = Read-JsonFile $watchLockPath
    if (-not (Test-HelperWatchProcess $watchLock)) {
        if ($watchLock) {
            Write-Host 'Watch process identity could not be verified; no process was stopped.'
        } else {
            Write-Host 'No Codex helper watch found.'
        }
        return
    }
    # The worker closes its own Chrome pipe before exiting. Never kill by name.
    $request = [ordered]@{ pid = [int]$watchLock.pid; started_at = [string]$watchLock.started_at }
    $tempPath = "$stopPath.$PID.tmp"
    $request | ConvertTo-Json -Compress | Set-Content -LiteralPath $tempPath -Encoding UTF8
    Move-Item -LiteralPath $tempPath -Destination $stopPath -Force
    for ($attempt = 0; $attempt -lt 40; $attempt++) {
        Start-Sleep -Milliseconds 250
        if (-not (Test-HelperWatchProcess $watchLock)) {
            Write-Host 'Stopped Codex helper watch.'
            return
        }
    }
    throw 'Codex helper stop is still pending. Retry status after the current request timeout.'
}

function Start-HiddenWatch {
    $watchLock = Read-JsonFile $watchLockPath
    if (Test-HelperWatchProcess $watchLock) {
        Write-Host ("Codex helper watch already running (PID {0})." -f [int]$watchLock.pid)
        return
    }
    $nodePath = (Get-Command node -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
    $arguments = '"{0}" watch' -f $helperScriptPath
    $child = Start-Process -FilePath $nodePath -WorkingDirectory $helperDir -ArgumentList $arguments -WindowStyle Hidden -PassThru
    Start-Sleep -Seconds 1
    if ($child.HasExited) { throw 'Codex helper watch exited during startup. Run status for the sanitized failure state.' }
    Write-Host ("Started Codex helper watch in background (PID {0})." -f $child.Id)
    Show-Status
}

function Reset-HelperProfile {
    Stop-HelperWatch
    # Node holds both OS locks and validates the resolved profile before reset.
    $nodePath = (Get-Command node -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
    & $nodePath $helperScriptPath reset
    if ($LASTEXITCODE -ne 0) { throw 'Codex profile reset was refused. Close its Chrome window and retry after other helper work ends.' }
}

switch ($Mode) {
    'start' { Start-HiddenWatch; exit 0 }
    'status' { Show-Status; exit 0 }
    'stop' { Stop-HelperWatch; Show-Status; exit 0 }
    'reset' { Reset-HelperProfile; exit 0 }
    default {
        $nodePath = (Get-Command node -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
        & $nodePath $helperScriptPath $Mode
        exit $LASTEXITCODE
    }
}
