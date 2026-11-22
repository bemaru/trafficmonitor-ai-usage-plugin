param(
    [ValidateSet('login', 'once', 'watch', 'start', 'status', 'stop')]
    [string]$Mode = 'watch'
)

$ErrorActionPreference = 'Stop'

$helperDirCandidates = @(
    (Join-Path $PSScriptRoot 'helper\claude-web-helper'),
    (Join-Path (Split-Path $PSScriptRoot -Parent) 'helper\claude-web-helper')
)

$helperDir = $helperDirCandidates |
    Where-Object { Test-Path (Join-Path $_ 'package.json') } |
    Select-Object -First 1

$helperDir = if ($helperDir) { (Resolve-Path $helperDir).Path } else { $null }
$baseDir = if ($env:LOCALAPPDATA) {
    Join-Path $env:LOCALAPPDATA 'trafficmonitor-claude-usage-plugin'
} else {
    Join-Path $env:USERPROFILE '.cache\trafficmonitor-claude-usage-plugin'
}
$usagePath = Join-Path $baseDir 'claude-web-usage.json'
$statusPath = Join-Path $baseDir 'claude-web-helper-status.json'
$watchLockPath = Join-Path $baseDir 'claude-web-helper-watch.lock'

if (-not $helperDir) {
    throw ("Helper package.json not found. Checked: {0}" -f ($helperDirCandidates -join ', '))
}

function Get-RelativeAgeText {
    param([object]$Timestamp)

    if (-not $Timestamp) {
        return 'unknown'
    }

    $timestampValue = [datetime]$Timestamp
    $span = [datetime]::UtcNow - $timestampValue.ToUniversalTime()
    if ($span.TotalSeconds -lt 0) {
        return 'just now'
    }

    if ($span.TotalMinutes -lt 1) {
        return ('{0:n0}s ago' -f $span.TotalSeconds)
    }

    if ($span.TotalHours -lt 1) {
        return ('{0:n0}m ago' -f $span.TotalMinutes)
    }

    return ('{0:n1}h ago' -f $span.TotalHours)
}

function Format-FileStatus {
    param([string]$Path)

    if (-not (Test-Path $Path)) {
        return "$Path [missing]"
    }

    $item = Get-Item $Path
    return "$Path [$([int64]$item.Length) bytes, $($item.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss')), $(Get-RelativeAgeText $item.LastWriteTime)]"
}

function Test-HelperWatchProcess {
    param(
        [int]$ProcessId,
        [object]$WatchLock
    )

    if ($ProcessId -le 0 -or -not $WatchLock -or [string]$WatchLock.mode -ne 'watch') {
        return $false
    }

    try {
        $process = Get-CimInstance Win32_Process -Filter "ProcessId = $ProcessId" -ErrorAction Stop
        if (-not $process -or $process.Name -ne 'node.exe') {
            return $false
        }

        $commandLine = [string]$process.CommandLine
        if ($commandLine -notmatch '(?i)index\.mjs' -or $commandLine -notmatch '(?i)(^|\s)watch(\s|$)') {
            return $false
        }

        $lockStartedAt = [DateTimeOffset]::Parse(
            [string]$WatchLock.started_at,
            [System.Globalization.CultureInfo]::InvariantCulture,
            [System.Globalization.DateTimeStyles]::AssumeUniversal
        ).ToUniversalTime()
        $processStartedAt = ([DateTimeOffset]$process.CreationDate).ToUniversalTime()
        $startDifference = [Math]::Abs(($lockStartedAt - $processStartedAt).TotalSeconds)
        return $startDifference -le 30
    } catch {
        return $false
    }
}

function Get-WatchLockData {
    if (-not (Test-Path $watchLockPath)) {
        return $null
    }

    try {
        return Get-Content $watchLockPath -Raw | ConvertFrom-Json
    } catch {
        return $null
    }
}

function Remove-StaleWatchLock {
    $watchLock = Get-WatchLockData
    if (-not $watchLock) {
        if (Test-Path $watchLockPath) {
            Remove-Item $watchLockPath -Force -ErrorAction SilentlyContinue
        }
        return
    }

    $watchPid = 0
    [void][int]::TryParse([string]$watchLock.pid, [ref]$watchPid)
    if (Test-HelperWatchProcess -ProcessId $watchPid -WatchLock $watchLock) {
        return
    }

    Remove-Item $watchLockPath -Force -ErrorAction SilentlyContinue
}

function Get-HelperNodeProcesses {
    Get-CimInstance Win32_Process |
        Where-Object {
            $_.Name -eq 'node.exe' -and
            $_.CommandLine -match 'index\.mjs' -and
            $_.CommandLine -match '\b(login|once|watch)\b'
        } |
        Sort-Object ProcessId
}

function Show-Status {
    Write-Host "Helper dir: $helperDir"
    Write-Host "Profile dir: $(Join-Path $baseDir 'claude-browser-profile')"
    Write-Host (Format-FileStatus $usagePath)
    Write-Host (Format-FileStatus $statusPath)
    Write-Host (Format-FileStatus $watchLockPath)

    $watchLock = Get-WatchLockData
    if ($watchLock) {
        $watchPid = 0
        [void][int]::TryParse([string]$watchLock.pid, [ref]$watchPid)
        $running = Test-HelperWatchProcess -ProcessId $watchPid -WatchLock $watchLock
        Write-Host ("Watch: pid={0}, mode={1}, refresh_ms={2}, started_at={3}, running={4}" -f $watchPid, $watchLock.mode, $watchLock.refresh_ms, $watchLock.started_at, $running)
        if (-not $running) {
            Write-Host 'Watch lock is stale.'
        }
    } else {
        Write-Host 'Watch: none'
    }

    if (Test-Path $statusPath) {
        try {
            $status = Get-Content $statusPath -Raw | ConvertFrom-Json
            Write-Host "Status: $($status.state)"
            Write-Host "Updated: $($status.updated_at)"
            if ($status.organization_name) {
                Write-Host "Org: $($status.organization_name)"
            }
            if ($status.error) {
                Write-Host "Error: $($status.error)"
            }
        } catch {
            Write-Host "Status: unreadable ($($_.Exception.Message))"
        }
    }

    $processes = Get-HelperNodeProcesses
    if ($processes) {
        Write-Host 'Helper node processes:'
        foreach ($process in $processes) {
            Write-Host ("  PID {0}: {1}" -f $process.ProcessId, $process.CommandLine)
        }
    } else {
        Write-Host 'Helper node processes: none'
    }
}

function Stop-HelperProcesses {
    $stopped = $false
    $watchLock = Get-WatchLockData
    if ($watchLock) {
        $watchPid = 0
        [void][int]::TryParse([string]$watchLock.pid, [ref]$watchPid)
        if (Test-HelperWatchProcess -ProcessId $watchPid -WatchLock $watchLock) {
            Stop-Process -Id $watchPid -Force
            Start-Sleep -Milliseconds 500
            if (-not (Test-HelperWatchProcess -ProcessId $watchPid -WatchLock $watchLock)) {
                Remove-Item $watchLockPath -Force -ErrorAction SilentlyContinue
            }
            Write-Host "Stopped helper watch PID $watchPid"
            $stopped = $true
        } else {
            Remove-Item $watchLockPath -Force -ErrorAction SilentlyContinue
            Write-Host 'Removed stale helper watch lock.'
        }
    } elseif (Test-Path $watchLockPath) {
        Remove-Item $watchLockPath -Force -ErrorAction SilentlyContinue
        Write-Host 'Removed unreadable helper watch lock.'
        $stopped = $true
    }

    if (-not $stopped) {
        Write-Host 'No helper processes found.'
    }
}

function Start-HiddenWatch {
    Remove-StaleWatchLock

    $watchLock = Get-WatchLockData
    if ($watchLock) {
        $watchPid = 0
        [void][int]::TryParse([string]$watchLock.pid, [ref]$watchPid)
        if (Test-HelperWatchProcess -ProcessId $watchPid -WatchLock $watchLock) {
            Write-Host "Helper watch already running (PID $watchPid)."
            return
        }
    }

    $nodePath = (Get-Command node -CommandType Application).Source
    $helperScriptPath = Join-Path $helperDir 'index.mjs'
    $escapedHelperScriptPath = $helperScriptPath.Replace('"', '\"')
    $arguments = "--disable-warning=ExperimentalWarning `"$escapedHelperScriptPath`" watch"
    $child = Start-Process -FilePath $nodePath -WorkingDirectory $helperDir -ArgumentList $arguments -WindowStyle Hidden -PassThru
    Start-Sleep -Seconds 1
    Write-Host "Started helper watch in background (PID $($child.Id))."
    Show-Status
}

function Test-NodeSqliteSupport {
    node --disable-warning=ExperimentalWarning -p "require('node:sqlite'); 'ok'" > $null 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw 'Node.js 22+ with node:sqlite support is required for the Claude web helper.'
    }
}

Push-Location $helperDir
try {
    switch ($Mode) {
        'start' {
            Test-NodeSqliteSupport
            Start-HiddenWatch
            exit 0
        }
        'status' {
            Remove-StaleWatchLock
            Show-Status
            exit 0
        }
        'stop' {
            Stop-HelperProcesses
            Show-Status
            exit 0
        }
        default {
            Test-NodeSqliteSupport
            node --disable-warning=ExperimentalWarning index.mjs $Mode
            exit $LASTEXITCODE
        }
    }
}
finally {
    Pop-Location
}
