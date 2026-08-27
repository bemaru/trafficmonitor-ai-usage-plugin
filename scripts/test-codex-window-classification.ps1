param(
    [ValidateSet('x64', 'Win32')]
    [string]$Platform = 'x64',
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path $PSScriptRoot -Parent
$solutionPath = Join-Path $repoRoot 'ClaudeUsagePlugin.sln'
$vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$msbuildPath = $null

$msbuildCommand = Get-Command MSBuild.exe -CommandType Application -ErrorAction SilentlyContinue
if ($msbuildCommand) {
    $msbuildPath = $msbuildCommand.Source
} elseif (Test-Path $vswherePath) {
    $msbuildPath = & $vswherePath -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' |
        Select-Object -First 1
}

if (-not $msbuildPath) {
    throw 'MSBuild.exe was not found.'
}

& $msbuildPath $solutionPath /t:Build "/p:Configuration=$Configuration" "/p:Platform=$Platform" /m /nologo
if ($LASTEXITCODE -ne 0) {
    throw "Build failed with exit code $LASTEXITCODE."
}

$pluginOutputRoot = if ($Platform -eq 'Win32') {
    Join-Path $repoRoot "build\$Configuration"
} else {
    Join-Path $repoRoot "build\$Platform\$Configuration"
}
$testOutputRoot = Join-Path $repoRoot "build\$Platform\$Configuration"
$pluginPath = Join-Path $pluginOutputRoot 'plugins\ClaudeUsagePlugin.dll'
$testPath = Join-Path $testOutputRoot 'tests\CodexUsagePluginTests.exe'
$scenarios = @(
    'weekly-primary',
    'both-windows',
    'swapped-windows',
    'legacy-no-window',
    'unknown-window',
    'five-hour-primary',
    'five-hour-secondary',
    'both-windows-zero',
    'remaining-percent-swapped',
    'weekly-to-both',
    'weekly-to-swapped',
    'both-to-weekly'
)

foreach ($scenario in $scenarios) {
    & $testPath $pluginPath $scenario
    if ($LASTEXITCODE -ne 0) {
        throw "Scenario '$scenario' failed with exit code $LASTEXITCODE."
    }
}

Write-Host "All Codex usage window classification tests passed ($Configuration|$Platform)."
