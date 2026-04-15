# ClaudeUsagePlugin

Private standalone repository for building the `ClaudeUsagePlugin.dll` plugin for [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor).

Versioning and release notes are tracked in [CHANGELOG.md](CHANGELOG.md).

## Scope

- Claude and Codex account usage
- Claude tries the OAuth usage endpoint first, then falls back to the freshest available local cache or optional Claude Code statusline bridge data

## Runtime compatibility

- Windows TrafficMonitor plugin DLL only
- TrafficMonitor plugin API v7
- Use the plugin DLL that matches the installed TrafficMonitor architecture:
  - `x64` plugin for `x64` TrafficMonitor
  - `x86` plugin for `x86` TrafficMonitor
- Official release assets are currently provided for `x64` and `x86`
- TrafficMonitor itself is not bundled by this repo

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
- Caches successful Claude usage responses locally and reuses them for a short period
- If the OAuth usage endpoint is rate-limited or temporarily unavailable, the plugin falls back to the freshest available local Claude snapshot
- The optional Claude Code statusline bridge writes one such local snapshot to `%LOCALAPPDATA%\trafficmonitor-claude-usage-plugin\claude-statusline.json`
- The included `scripts\claude-statusline-wrapper.ps1` keeps `ccstatusline` working while also writing that bridge cache

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

- Claude OAuth API success refresh: 180 seconds
- Claude other failure retry: 30 seconds
- Claude rate limit retry: respects `Retry-After` when present, otherwise falls back to 5 minutes
- Claude statusline cache poll: 5 seconds while the OAuth API is in `Retry-After` backoff and a fresh bridge cache exists
- Claude statusline / cache fresh TTL: 180 seconds
- Codex success refresh: 60 seconds
- Codex failure retry: 5 seconds

## Install for TrafficMonitor users

1. Install the official TrafficMonitor release separately.
2. Download the plugin asset that matches your TrafficMonitor architecture:
   - `ClaudeUsagePlugin_v*_x64.zip` for `x64` TrafficMonitor
   - `ClaudeUsagePlugin_v*_x86.zip` for `x86` TrafficMonitor
3. Copy `ClaudeUsagePlugin.dll` into the TrafficMonitor `plugins` directory.
   - Example: if TrafficMonitor is unpacked at `D:\Apps\TrafficMonitor`, copy the DLL to `D:\Apps\TrafficMonitor\plugins\ClaudeUsagePlugin.dll`
4. Restart TrafficMonitor.
5. Open plug-in management and confirm `Claude/Codex Usage` is loaded.
6. Enable `Claude 5h`, `Claude 7d`, `Codex 5h`, `Codex 7d` in the displayed items list.

If the plug-in loads but the items do not appear, check the DLL architecture first. An `x64` DLL will not load into `x86` TrafficMonitor, and vice versa.
This repo and its releases ship only the plug-in DLL, not TrafficMonitor itself.

## Build requirements

- Windows
- Visual Studio 2022 or Build Tools 2022
- Desktop development with C++
- MSVC `v143` toolset
- MFC for the `v143` toolset (`UseOfMfc=Dynamic`)
- Windows 10 SDK / compatible Windows SDK selected by Visual Studio

## Build from source

Open [ClaudeUsagePlugin.sln](ClaudeUsagePlugin.sln) in Visual Studio and build `Release|x64` or `Release|Win32`, or run:

```powershell
MSBuild.exe .\ClaudeUsagePlugin.sln /t:ClaudeUsagePlugin /p:Configuration=Release /p:Platform=x64
```

Build output:

- `build\x64\Release\plugins\ClaudeUsagePlugin.dll`
- `build\Release\plugins\ClaudeUsagePlugin.dll` for `Release|Win32`

The project file also contains `ARM64EC` configurations, but the published release assets are currently only `x64` and `x86`.

## Environment and path assumptions

- TrafficMonitor runs on Windows, so every runtime path must be readable from Windows.
- Claude API auth uses `%USERPROFILE%\.claude\.credentials.json` by default.
- `CLAUDE_CONFIG_DIR` is supported, but it must point to a Windows-readable directory. A Linux-only WSL path such as `/home/<user>/.claude` will not work for the Windows plugin process.
- Codex state uses `%USERPROFILE%\.codex` by default.
- `CODEX_HOME` is supported, but it must be visible to the Windows TrafficMonitor process. Setting `CODEX_HOME` only inside WSL is not enough unless TrafficMonitor inherits an equivalent Windows-side path.
- WSL-style `/mnt/c/...` paths are accepted only when they resolve back to Windows storage.
- If you override `CLAUDE_CONFIG_DIR` or `CODEX_HOME`, set them in the Windows environment before launching TrafficMonitor. WSL-only shell exports are not visible to the plugin.

## Optional Claude statusline bridge

If you use Claude Code on the same Windows account, the official statusline `rate_limits` payload provides a useful local fallback source while the undocumented OAuth usage endpoint is unavailable or rate-limited.

Windows-native Claude Code:

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
- Writes `rate_limits` to `%LOCALAPPDATA%\trafficmonitor-claude-usage-plugin\claude-statusline.json`
- Forwards the same stdin payload to `bunx -y ccstatusline@latest`, so the existing `ccstatusline` output keeps working
- Requires `bunx` to be available on Windows if you want the forwarded `ccstatusline` output
- The bridge becomes usable only after Claude Code has produced at least one response with `rate_limits`; until then the plugin continues to rely on the OAuth usage API

WSL Claude Code:

1. Copy `scripts/claude-statusline-wrapper.sh` to `~/.claude/trafficmonitor-statusline-wrapper.sh`.
2. Make it executable: `chmod +x ~/.claude/trafficmonitor-statusline-wrapper.sh`
3. Update `~/.claude/settings.json`:

```json
{
  "statusLine": {
    "type": "command",
    "command": "~/.claude/trafficmonitor-statusline-wrapper.sh",
    "padding": 0,
    "refreshInterval": 5
  }
}
```

The WSL wrapper writes the cache into the Windows-readable `%LOCALAPPDATA%\trafficmonitor-claude-usage-plugin` path through `/mnt/c/...`, then forwards stdin to `npx -y ccstatusline@2.2.7`.

WSL bridge prerequisites:

- `python3`
- `npx`
- `cmd.exe` and `wslpath` available from WSL
- Claude Code restarted after changing `~/.claude/settings.json`

## Codex setup

- By default no extra setup is needed if Codex writes to `%USERPROFILE%\.codex` on Windows.
- If your Codex state lives somewhere else, set `CODEX_HOME` in the Windows environment seen by TrafficMonitor.
- If you use Codex through WSL, make sure the actual log and session files are stored on Windows-readable storage such as `/mnt/c/...`.

## Verification

After installation or setup, check the following:

1. TrafficMonitor plug-in management shows `Claude/Codex Usage`.
2. Display settings lists `Claude 5h`, `Claude 7d`, `Codex 5h`, `Codex 7d`.
3. The taskbar items show percentages instead of `--`.
4. The tooltip shows reset timing for any source that exposes reset metadata.
5. If you enabled the Claude statusline bridge, `%LOCALAPPDATA%\trafficmonitor-claude-usage-plugin\claude-statusline.json` updates after a Claude Code response.

## Constraints

- This depends on Claude's local credential file layout.
- The Claude OAuth usage endpoint used as fallback is undocumented and may change.
- Claude cached fallback values come only from the plugin's own last successful API snapshot and can be stale when the live API is unavailable.
- The Claude statusline bridge only updates while Claude Code is running and emitting statusline payloads.
- After changing the `statusLine` command, open Claude Code and get at least one assistant response so the bridge cache is created.
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

- `Claude usage API HTTP 401 (login required)`:
  The current Windows-side Claude OAuth token is expired or no longer accepted by the API.

- `Claude usage API HTTP 403 (access denied)`:
  The token was found, but the API refused the request for that account or org context.

- `Claude usage API HTTP 429 rate limited`:
  The endpoint rejected requests temporarily. The plugin will wait until `Retry-After` and may show the freshest available local Claude snapshot while backoff is active.

- Claude values do not match the Claude Code UI:
  The plugin now prefers the OAuth usage API when it is available. If you are depending on the local Claude Code fallback path, verify that the statusline wrapper is installed, Claude Code is actually running, and `%LOCALAPPDATA%\trafficmonitor-claude-usage-plugin\claude-statusline.json` is updating.

- `Claude usage API returned unexpected data`:
  The response schema changed and the plugin needs an update.

- `Claude usage API request failed`:
  Network, TLS, or endpoint reachability failed. Cached Claude values may still be shown if a usable cache exists.

- `Codex config directory not found`:
  `CODEX_HOME` or `%USERPROFILE%\.codex` could not be resolved from the Windows TrafficMonitor process.

- `Codex logs_2.sqlite unavailable`:
  The local Codex SQLite store exists but could not be read.

- `Codex sessions JSONL returned no rate limits`:
  Codex local session logs were found, but no rate-limit payload was present yet.

- Codex usage does not appear:
  Verify that `CODEX_HOME` points to the Codex store actually being written by your Windows or WSL session, and that the resolved path is readable from Windows.
