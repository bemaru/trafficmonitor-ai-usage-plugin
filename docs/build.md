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

## Regression Tests

Run the Codex DLL-level regression tests on both supported architectures:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\test-codex-window-classification.ps1 -Platform x64
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\test-codex-window-classification.ps1 -Platform Win32
```

Each command builds the solution and checks 12 synthetic session histories
through the plugin's item-value and tooltip interfaces. Coverage includes
weekly-only limits, both windows in either position, a 5-hour-only limit, zero
usage, remaining-to-used conversion, legacy payloads, and unknown windows.
Same-file histories also cover a weekly-only snapshot followed by returning
5-hour limits, and the reverse transition back to weekly-only.

These tests use a temporary `CODEX_HOME`; they do not require an account login,
start the Claude helper, or modify the installed TrafficMonitor. They validate
the recorded payloads at load time, not a live account's rollout or timed refresh.

Run the helper watch-lock regression test:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\test-claude-web-helper-watch-lock.ps1
```

The test confirms that a stale lock cannot cause an unrelated reused PID to be
treated as, or terminated as, the helper watcher.

## Build Output

- `build\x64\Release\plugins\ClaudeUsagePlugin.dll`
- `build\x64\Release\plugins\ClaudeUsagePlugin\claude-web-helper.ps1`
- `build\x64\Release\plugins\ClaudeUsagePlugin\helper\claude-web-helper\...`
- `build\Release\plugins\ClaudeUsagePlugin.dll`
- `build\Release\plugins\ClaudeUsagePlugin\claude-web-helper.ps1`
- `build\Release\plugins\ClaudeUsagePlugin\helper\claude-web-helper\...`

The project file also contains `ARM64EC` configurations, but the published release assets are currently only `x64` and `x86`.

## Packaging Notes

Package the built `plugins` output as one zip per architecture.

Recommended asset names:

- `TrafficMonitorAIUsageLimits_v<version>_x64.zip`
- `TrafficMonitorAIUsageLimits_v<version>_x86.zip`

Use [release-checklist.md](release-checklist.md) for the release flow and [release-notes-template.md](release-notes-template.md) for the GitHub release text.
