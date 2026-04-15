# ClaudeUsagePlugin

Private standalone repository for building the `ClaudeUsagePlugin.dll` plugin for [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor).

Versioning and release notes are tracked in [CHANGELOG.md](CHANGELOG.md).

## Scope

- Claude and Codex account usage
- Claude prefers an optional Claude Code statusline bridge when configured, then falls back to the OAuth usage endpoint and cached data

## What the plugin shows

- `C5h`: current 5-hour usage percentage
- `C7d`: current 7-day usage percentage
- `X5h`: current Codex 5-hour usage percentage
- `X7d`: current Codex 7-day usage percentage

Tooltip text also shows reset timing when the upstream data exposes it.

## How it works

Claude usage:

- Prefers a local Claude Code statusline bridge cache at `%LOCALAPPDATA%\trafficmonitor-ai-usage-plugin\claude-statusline.json` when available
- The included `scripts\claude-statusline-wrapper.ps1` keeps `ccstatusline` working while also writing that bridge cache
- Reads the local Claude OAuth access token from `%USERPROFILE%\.claude\.credentials.json`
- Or from `CLAUDE_CONFIG_DIR\.credentials.json` if `CLAUDE_CONFIG_DIR` is set
- Sends a read-only request to `https://api.anthropic.com/api/oauth/usage`
- Caches successful Claude usage responses locally and reuses them for a short period
- If the OAuth usage endpoint is rate-limited or temporarily unavailable, the plugin can fall back to the most recent cached usage snapshot

Codex usage:

- Reads local Codex usage data from `%USERPROFILE%\.codex\logs_2.sqlite`
- Falls back to `%USERPROFILE%\.codex\sessions\**\*.jsonl`
- Respects `CODEX_HOME` when set, including WSL-style `/mnt/c/...` paths pointing back to Windows

`CODEX_HOME` notes:

- If `CODEX_HOME` is not set, the plugin uses `%USERPROFILE%\.codex`
- Set `CODEX_HOME` if your Codex state lives somewhere else
- Windows path example: `C:\Users\<user>\.codex`
- WSL path example: `/mnt/c/Users/<user>/.codex`
- The plugin runs on Windows, so `CODEX_HOME` must resolve to a Windows-accessible location
- A Linux-only path such as `/home/<user>/.codex` will not be readable from TrafficMonitor on Windows

Refresh behavior:

- Claude statusline / cache fresh TTL: 180 seconds
- Claude OAuth API success refresh: 180 seconds
- Claude other failure retry: 30 seconds
- Claude rate limit retry: respects `Retry-After` when present, otherwise falls back to 5 minutes
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

## Optional Claude statusline bridge

If you use Claude Code on the same Windows account, the preferred Claude source is the official statusline `rate_limits` payload. This avoids relying on the undocumented OAuth usage endpoint during active Claude Code sessions.

1. Copy `scripts\claude-statusline-wrapper.ps1` to `%USERPROFILE%\.claude\trafficmonitor-statusline-wrapper.ps1`.
2. Update `%USERPROFILE%\.claude\settings.json`:

```json
{
  "statusLine": {
    "type": "command",
    "command": "powershell -NoProfile -ExecutionPolicy Bypass -File C:\\Users\\<user>\\.claude\\trafficmonitor-statusline-wrapper.ps1",
    "padding": 0
  }
}
```

This wrapper:

- Reads the official Claude Code statusline JSON from stdin
- Writes `rate_limits` to `%LOCALAPPDATA%\trafficmonitor-ai-usage-plugin\claude-statusline.json`
- Forwards the same stdin payload to `bunx -y ccstatusline@latest`, so the existing `ccstatusline` output keeps working

## Constraints

- This depends on Claude's local credential file layout.
- The Claude OAuth usage endpoint used as fallback is undocumented and may change.
- Claude cached fallback values come only from the plugin's own last successful API snapshot and can be stale when the live API is unavailable.
- The Claude statusline bridge only updates while Claude Code is running and emitting statusline payloads.
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
  The endpoint rejected requests temporarily. The plugin will wait until `Retry-After` and may show the plugin's most recent Claude cache if available.

- Claude values do not match the Claude Code UI:
  Verify that the statusline wrapper is installed, Claude Code is actually running, and `%LOCALAPPDATA%\trafficmonitor-ai-usage-plugin\claude-statusline.json` is updating.

- `Claude usage API returned unexpected data`:
  The response schema changed and the plugin needs an update.

- `Claude usage API request failed`:
  Network, TLS, or endpoint reachability failed. Cached Claude values may still be shown if a usable cache exists.

- `Codex config directory not found`:
  `CODEX_HOME` or `%USERPROFILE%\.codex` could not be resolved.

- `Codex logs_2.sqlite unavailable`:
  The local Codex SQLite store exists but could not be read.

- `Codex sessions JSONL returned no rate limits`:
  Codex local session logs were found, but no rate-limit payload was present yet.

- Codex usage does not appear:
  Verify that `CODEX_HOME` points to the Codex store actually being written by your Windows or WSL session, and that the resolved path is readable from Windows.
