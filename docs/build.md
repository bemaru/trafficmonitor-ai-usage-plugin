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

All Codex tests use a temporary `CODEX_HOME`; they do not require an account login,
start the Claude helper, or modify the installed TrafficMonitor. They validate
synthetic payloads, not a live account's rollout or the TrafficMonitor taskbar UI.
The default commands above check snapshots at load time only.

### Timed Refresh (Opt-in)

Add `-IncludeRefreshTests` to also verify updates while the same DLL instance
remains loaded:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\test-codex-window-classification.ps1 -Platform x64 -IncludeRefreshTests
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\test-codex-window-classification.ps1 -Platform Win32 -IncludeRefreshTests
```

This runs the 12 default scenarios plus `refresh-weekly-both-weekly`. The added
scenario first asserts `X5h=--` / `X7d=30%`, then appends newer records to the same
temporary session file while keeping the DLL loaded. After each append, it waits
65 seconds for the real 60-second success refresh interval and asserts both item
values and the tooltip:

- Returning both windows: `X5h=12%` / `X7d=34%`.
- Returning to weekly-only: `X5h=--` / `X7d=35%`; the old 5-hour value must disappear.

The two waits add about 130 seconds per architecture. The test does not shorten
the production interval or reinitialize the DLL. A missing refresh or a retained
old 5-hour value fails the test; a second read of unchanged data is not counted
as evidence that updates work.

### Claude Helper Watch Lock

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
