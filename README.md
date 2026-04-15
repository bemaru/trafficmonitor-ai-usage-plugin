# ClaudeUsagePlugin

Private standalone repository for building the `ClaudeUsagePlugin.dll` plugin for [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor).

Versioning and release notes are tracked in [CHANGELOG.md](CHANGELOG.md).

## Scope

- Claude and Codex account usage
- If refresh fails, the plugin falls back to `unavailable` instead of keeping stale values

## What the plugin shows

- `C5h`: current 5-hour usage percentage
- `C7d`: current 7-day usage percentage
- `X5h`: current Codex 5-hour usage percentage
- `X7d`: current Codex 7-day usage percentage

Tooltip text also shows reset timing when the upstream data exposes it.

## How it works

Claude usage:

- Reads the local Claude OAuth access token from `%USERPROFILE%\.claude\.credentials.json`
- Or from `CLAUDE_CONFIG_DIR\.credentials.json` if `CLAUDE_CONFIG_DIR` is set
- Sends a read-only request to `https://api.anthropic.com/api/oauth/usage`

Codex usage:

- Reads local Codex usage data from `%USERPROFILE%\.codex\logs_2.sqlite`
- Falls back to `%USERPROFILE%\.codex\sessions\**\*.jsonl`
- Respects `CODEX_HOME` when set, including WSL-style `/mnt/c/...` paths pointing back to Windows

Refresh behavior:

- Claude success refresh: 60 seconds
- Claude failure retry: 5 seconds
- Codex success refresh: 60 seconds
- Codex failure retry: 5 seconds

## Requirements

- Windows
- Visual Studio 2022 or Build Tools 2022
- Desktop development with C++
- MFC for v143 toolset
- Windows 10 SDK
- Official TrafficMonitor installed separately

## Build

Open [ClaudeUsagePlugin.sln](ClaudeUsagePlugin.sln) in Visual Studio and build `Release|x64` or `Release|Win32`, or run:

```powershell
MSBuild.exe .\ClaudeUsagePlugin.sln /t:ClaudeUsagePlugin /p:Configuration=Release /p:Platform=x64
```

Build output:

- `build\x64\Release\plugins\ClaudeUsagePlugin.dll`
- `build\Release\plugins\ClaudeUsagePlugin.dll` for `Release|Win32`

## Install

1. Install the official TrafficMonitor release.
2. Build this repository.
3. Copy `build\x64\Release\plugins\ClaudeUsagePlugin.dll` into the TrafficMonitor `plugins` directory.
4. Restart TrafficMonitor.
5. Enable `Claude 5h`, `Claude 7d`, `Codex 5h`, `Codex 7d` in the displayed items list.

This repo only ships the plugin DLL. It does not bundle TrafficMonitor itself.

## Constraints

- This depends on Claude's local credential file layout.
- This depends on Anthropic keeping the current usage endpoint and response shape compatible.
- Codex usage currently comes from local Codex state, not an official OpenAI usage API.
- Codex values update only after Codex itself writes fresh rate-limit data locally.
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

- `Codex config directory not found`:
  `CODEX_HOME` or `%USERPROFILE%\.codex` could not be resolved.

- `Codex logs_2.sqlite unavailable`:
  The local Codex SQLite store exists but could not be read.

- `Codex sessions JSONL returned no rate limits`:
  Codex local session logs were found, but no rate-limit payload was present yet.
