param([ValidateSet('x64', 'x86')][string]$Architecture = 'x64')
$ErrorActionPreference = 'Stop'
$repoDir = Split-Path $PSScriptRoot -Parent
$vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsDir = & $vswherePath -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsDir) { throw 'Visual Studio C++ tools were not found.' }
$vcvarsPath = Join-Path $vsDir 'VC\Auxiliary\Build\vcvarsall.bat'
$testDir = Join-Path $repoDir "build\codex-auto-start-$Architecture"
New-Item -ItemType Directory -Path $testDir -Force | Out-Null
$fixtureDir = Join-Path $testDir ([guid]::NewGuid().ToString('N'))
$testSource = Join-Path $PSScriptRoot 'codex-auto-start.cpp'
$testExe = Join-Path $testDir 'codex-auto-start.exe'
$testObj = Join-Path $testDir 'codex-auto-start.obj'
$buildScript = Join-Path $testDir 'build-test.cmd'
$command = @"
@echo off
chcp 65001 >nul
call "$vcvarsPath" $Architecture >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /DUNICODE /D_UNICODE "$testSource" /Fo"$testObj" /Fe"$testExe"
if errorlevel 1 exit /b 1
"$testExe" "$fixtureDir"
exit /b %errorlevel%
"@
$command = $command.Replace("`r`n", "`n").Replace("`n", "`r`n")
[System.IO.File]::WriteAllText($buildScript, $command, [System.Text.UTF8Encoding]::new($false))
& $env:ComSpec /d /c $buildScript
if ($LASTEXITCODE -ne 0) { throw "Codex automatic startup test failed ($LASTEXITCODE)." }
