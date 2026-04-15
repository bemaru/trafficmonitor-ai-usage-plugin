Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-PluginCacheDir {
    if (-not [string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) {
        return [System.IO.Path]::Combine($env:LOCALAPPDATA, 'trafficmonitor-claude-usage-plugin')
    }

    if (-not [string]::IsNullOrWhiteSpace($env:USERPROFILE)) {
        return [System.IO.Path]::Combine($env:USERPROFILE, '.cache', 'trafficmonitor-claude-usage-plugin')
    }

    return $null
}

function Write-StatuslineCache {
    param(
        [Parameter(Mandatory = $true)]
        [string] $InputJson
    )

    try {
        $payload = $InputJson | ConvertFrom-Json -Depth 32
        if ($null -eq $payload -or $null -eq $payload.rate_limits) {
            return
        }

        $cache = [ordered]@{}
        foreach ($window in @('five_hour', 'seven_day')) {
            $sourceWindow = $payload.rate_limits.$window
            if ($null -eq $sourceWindow) {
                continue
            }

            $targetWindow = [ordered]@{}
            if ($null -ne $sourceWindow.used_percentage) {
                $targetWindow.utilization = [double] $sourceWindow.used_percentage
            }
            $resetAtIso = $null
            if ($null -ne $sourceWindow.resets_at) {
                if ($sourceWindow.resets_at -is [DateTime]) {
                    $resetAtIso = $sourceWindow.resets_at.ToUniversalTime().ToString('o')
                }
                else {
                    $resetAtText = [string] $sourceWindow.resets_at
                    $resetAtSeconds = 0L
                    if ([long]::TryParse($resetAtText, [ref] $resetAtSeconds)) {
                        $resetAtIso = [DateTimeOffset]::FromUnixTimeSeconds($resetAtSeconds).UtcDateTime.ToString('o')
                    }
                    elseif (-not [string]::IsNullOrWhiteSpace($resetAtText)) {
                        try {
                            $resetAtIso = [DateTimeOffset]::Parse($resetAtText).ToUniversalTime().ToString('o')
                        }
                        catch {
                            $resetAtIso = $resetAtText
                        }
                    }
                }
            }
            if (-not [string]::IsNullOrWhiteSpace($resetAtIso)) {
                $targetWindow.resets_at = $resetAtIso
            }
            if ($targetWindow.Count -gt 0) {
                $cache[$window] = $targetWindow
            }
        }

        if ($cache.Count -eq 0) {
            return
        }

        $cache.updated_at = (Get-Date).ToUniversalTime().ToString('o')

        $cacheDir = Get-PluginCacheDir
        if ([string]::IsNullOrWhiteSpace($cacheDir)) {
            return
        }

        [System.IO.Directory]::CreateDirectory($cacheDir) | Out-Null
        $cachePath = [System.IO.Path]::Combine($cacheDir, 'claude-statusline.json')
        $json = $cache | ConvertTo-Json -Depth 8
        $utf8NoBom = [System.Text.UTF8Encoding]::new($false)
        [System.IO.File]::WriteAllText($cachePath, $json, $utf8NoBom)
    }
    catch {
        # Keep the status line functional even if the cache write fails.
    }
}

function Invoke-CcStatusline {
    param(
        [Parameter(Mandatory = $true)]
        [string] $InputJson
    )

    $bunxCommand = Get-Command bunx.exe -ErrorAction SilentlyContinue
    if ($null -eq $bunxCommand) {
        $bunxCommand = Get-Command bunx -ErrorAction SilentlyContinue
    }
    if ($null -eq $bunxCommand) {
        return
    }

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $bunxCommand.Source
    $startInfo.ArgumentList.Add('-y')
    $startInfo.ArgumentList.Add('ccstatusline@latest')
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardInput = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.CreateNoWindow = $true

    $process = [System.Diagnostics.Process]::Start($startInfo)
    try {
        $process.StandardInput.Write($InputJson)
        $process.StandardInput.Close()

        $stdout = $process.StandardOutput.ReadToEnd()
        $stderr = $process.StandardError.ReadToEnd()
        $process.WaitForExit()

        if (-not [string]::IsNullOrEmpty($stdout)) {
            [Console]::Out.Write($stdout)
        }
        if ($process.ExitCode -ne 0 -and -not [string]::IsNullOrEmpty($stderr)) {
            [Console]::Error.Write($stderr)
        }
    }
    finally {
        $process.Dispose()
    }
}

$inputJson = [Console]::In.ReadToEnd()
if ([string]::IsNullOrWhiteSpace($inputJson)) {
    $inputJson = ($input | Out-String)
}
Write-StatuslineCache -InputJson $inputJson
Invoke-CcStatusline -InputJson $inputJson
