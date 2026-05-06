param(
    [ValidateSet('login', 'status', 'reset')]
    [string]$Mode = 'status'
)

$ErrorActionPreference = 'Stop'

$baseDir = if ($env:LOCALAPPDATA) {
    Join-Path $env:LOCALAPPDATA 'trafficmonitor-claude-usage-plugin'
} else {
    Join-Path $env:USERPROFILE '.cache\trafficmonitor-claude-usage-plugin'
}
$profileDir = Join-Path $baseDir 'codex-browser-profile'
$statusPath = Join-Path $baseDir 'codex-web-helper-status.json'
$targetUrl = if ($env:CODEX_WEB_HELPER_URL) { $env:CODEX_WEB_HELPER_URL } else { 'https://chatgpt.com/codex' }

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

function Show-Status {
    Write-Host "Profile dir: $profileDir"
    Write-Host (Format-FileStatus $statusPath)

    if (Test-Path $statusPath) {
        try {
            $status = Get-Content $statusPath -Raw | ConvertFrom-Json
            Write-Host "Status: $($status.state)"
            Write-Host "Updated: $($status.updated_at)"
            if ($status.target_url) {
                Write-Host "Target URL: $($status.target_url)"
            }
            if ($status.error) {
                Write-Host "Error: $($status.error)"
            }
        } catch {
            Write-Host "Status: unreadable ($($_.Exception.Message))"
        }
    }
}

function Write-Status {
    param(
        [string]$State,
        [hashtable]$Details = @{}
    )

    if (-not (Test-Path $baseDir)) {
        New-Item -ItemType Directory -Path $baseDir -Force | Out-Null
    }

    $payload = [ordered]@{
        state = $State
        updated_at = [datetime]::UtcNow.ToString('o')
    }

    foreach ($key in $Details.Keys) {
        $payload[$key] = $Details[$key]
    }

    $tempPath = "$statusPath.tmp"
    $payload | ConvertTo-Json -Depth 8 | Set-Content -Path $tempPath -Encoding UTF8
    Move-Item -Path $tempPath -Destination $statusPath -Force
}

function Get-BrowserPath {
    if ($env:CODEX_WEB_HELPER_BROWSER -and (Test-Path $env:CODEX_WEB_HELPER_BROWSER)) {
        return $env:CODEX_WEB_HELPER_BROWSER
    }

    $candidates = @(
        'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe',
        'C:\Program Files\Microsoft\Edge\Application\msedge.exe',
        'C:\Program Files\Google\Chrome\Application\chrome.exe',
        'C:\Program Files (x86)\Google\Chrome\Application\chrome.exe'
    )

    return $candidates |
        Where-Object { Test-Path $_ } |
        Select-Object -First 1
}

function Open-LoginBrowser {
    if (-not (Test-Path $profileDir)) {
        New-Item -ItemType Directory -Path $profileDir -Force | Out-Null
    }

    $browserPath = Get-BrowserPath
    if (-not $browserPath) {
        throw 'Browser executable not found. Set CODEX_WEB_HELPER_BROWSER.'
    }

    $arguments = @(
        "--user-data-dir=$profileDir",
        '--new-window',
        $targetUrl
    )

    $process = Start-Process -FilePath $browserPath -ArgumentList $arguments -PassThru
    Write-Status 'login_browser_opened' @{
        profile_dir = $profileDir
        target_url = $targetUrl
        browser_path = $browserPath
        browser_pid = $process.Id
    }

    Write-Host 'Opened a normal browser window for Codex web login.'
    Write-Host 'Complete the login there. This PoC does not read cookies or fetch usage yet.'
}

switch ($Mode) {
    'login' {
        Open-LoginBrowser
        Show-Status
        exit 0
    }
    'reset' {
        if (Test-Path $profileDir) {
            Remove-Item -Path $profileDir -Recurse -Force
        }
        Write-Status 'reset' @{
            profile_dir = $profileDir
        }
        Show-Status
        exit 0
    }
    'status' {
        Show-Status
        exit 0
    }
}
