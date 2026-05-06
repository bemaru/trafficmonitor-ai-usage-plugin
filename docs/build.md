# Build Guide

## Requirements

- Windows
- Visual Studio 2022 or Build Tools 2022
- Desktop development with C++
- MSVC `v143` toolset
- MFC for the `v143` toolset (`UseOfMfc=Dynamic`)
- Windows SDK selected by Visual Studio

## Build

Open `ClaudeUsagePlugin.sln` in Visual Studio and build `Release|x64` or `Release|Win32`, or run:

```powershell
MSBuild.exe .\ClaudeUsagePlugin.sln /t:ClaudeUsagePlugin /p:Configuration=Release /p:Platform=x64
```

For `Win32`:

```powershell
MSBuild.exe .\ClaudeUsagePlugin.sln /t:ClaudeUsagePlugin /p:Configuration=Release /p:Platform=Win32
```

## Build Output

- `build\x64\Release\plugins\ClaudeUsagePlugin.dll`
- `build\x64\Release\plugins\ClaudeUsagePlugin\claude-web-helper.ps1`
- `build\x64\Release\plugins\ClaudeUsagePlugin\helper\claude-web-helper\...`
- `build\x64\Release\plugins\ClaudeUsagePlugin\codex-web-helper.ps1`
- `build\Release\plugins\ClaudeUsagePlugin.dll`
- `build\Release\plugins\ClaudeUsagePlugin\claude-web-helper.ps1`
- `build\Release\plugins\ClaudeUsagePlugin\helper\claude-web-helper\...`
- `build\Release\plugins\ClaudeUsagePlugin\codex-web-helper.ps1`

The project file also contains `ARM64EC` configurations, but the published release assets are currently only `x64` and `x86`.

## Packaging Notes

Package the built `plugins` output as one zip per architecture.

Recommended asset names:

- `TrafficMonitorAIUsageLimits_v<version>_x64.zip`
- `TrafficMonitorAIUsageLimits_v<version>_x86.zip`

Use [release-checklist.md](release-checklist.md) for the release flow and [release-notes-template.md](release-notes-template.md) for the GitHub release text.
