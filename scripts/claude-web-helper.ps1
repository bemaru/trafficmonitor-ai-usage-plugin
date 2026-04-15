param(
    [ValidateSet('login', 'once', 'watch')]
    [string]$Mode = 'watch'
)

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot '..')
$helperDir = Join-Path $repoRoot 'helper\claude-web-helper'

if (-not (Test-Path (Join-Path $helperDir 'package.json'))) {
    throw "Helper package.json not found at $helperDir"
}

Push-Location $helperDir
try {
    node -p "require('node:sqlite'); 'ok'" *> $null
    if ($LASTEXITCODE -ne 0) {
        throw 'Node.js 22+ with node:sqlite support is required for the Claude web helper.'
    }

    node --disable-warning=ExperimentalWarning index.mjs $Mode
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}
