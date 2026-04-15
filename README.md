# ClaudeUsagePlugin

Private standalone repository for building the `ClaudeUsagePlugin.dll` plugin for [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor).

Versioning and release notes are tracked in [CHANGELOG.md](/D:/jhkwak/repos/bemaru/trafficmonitor-claude-usage-plugin/CHANGELOG.md).

## Scope

- Claude account usage only
- Codex usage is not included yet
- If refresh fails, the plugin falls back to `unavailable` instead of keeping stale values

## What the plugin shows

- `C5h`: current 5-hour usage percentage
- `C7d`: current 7-day usage percentage

The values come from the Claude OAuth usage API for the signed-in Claude account.

## How it works

The plugin reads the local Claude OAuth access token from:

- `%USERPROFILE%\.claude\.credentials.json`
- or `CLAUDE_CONFIG_DIR\.credentials.json` if `CLAUDE_CONFIG_DIR` is set

It then sends a read-only request to:

- `https://api.anthropic.com/api/oauth/usage`

Refresh interval is currently one minute.

## Requirements

- Windows
- Visual Studio 2022 or Build Tools 2022
- Desktop development with C++
- MFC for v143 toolset
- Windows 10 SDK
- Official TrafficMonitor installed separately

## Build

Open [ClaudeUsagePlugin.sln](/D:/jhkwak/repos/bemaru/trafficmonitor-claude-usage-plugin/ClaudeUsagePlugin.sln) in Visual Studio and build `Release|x64`, or run:

```powershell
MSBuild.exe .\ClaudeUsagePlugin.sln /t:ClaudeUsagePlugin /p:Configuration=Release /p:Platform=x64
```

Build output:

- `build\x64\Release\plugins\ClaudeUsagePlugin.dll`

## Install

1. Install the official TrafficMonitor release.
2. Build this repository.
3. Copy `build\x64\Release\plugins\ClaudeUsagePlugin.dll` into the TrafficMonitor `plugins` directory.
4. Restart TrafficMonitor.
5. Enable `Claude 5h` and `Claude 7d` in the displayed items list.

This repo only ships the plugin DLL. It does not bundle TrafficMonitor itself.

## Constraints

- This depends on Claude's local credential file layout.
- This depends on Anthropic keeping the current usage endpoint and response shape compatible.
- This is best-effort integration, not an official Anthropic integration surface.
- This repository is intended to remain private for now.

## Tested compatibility

- TrafficMonitor plugin API v7
- Visual Studio 2022 / MSVC v143

## Troubleshooting

- `Claude access token not found`:
  Claude is not signed in locally, or the credential file location changed.

- `Claude login required`:
  The token is expired or no longer accepted by the API.

- `Claude usage API rate limited`:
  The endpoint rejected requests temporarily.

- `Claude usage API returned unexpected data`:
  The response schema changed and the plugin needs an update.

- `Claude usage API request failed`:
  Network, TLS, or endpoint reachability failed.
