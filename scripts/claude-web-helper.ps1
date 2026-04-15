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
    if (-not (Test-Path (Join-Path $helperDir 'node_modules\playwright-core'))) {
        npm install
        if ($LASTEXITCODE -ne 0) {
            throw 'npm install failed'
        }
    }

    node index.mjs $Mode
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}
