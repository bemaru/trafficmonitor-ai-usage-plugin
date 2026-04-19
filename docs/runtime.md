# Runtime and Helper Guide

## Runtime Model

Claude usage limits:

- Reads a fresh helper snapshot from `%LOCALAPPDATA%\trafficmonitor-claude-usage-plugin\claude-web-usage.json`
- The helper signs in through its own local Edge or Chrome profile, reads the saved Claude cookies from that profile, and calls `https://claude.ai/api/organizations/{lastActiveOrg}/usage`
- If the helper snapshot is missing or stale, Claude shows unavailable instead of falling back to stale values

Codex usage limits:

- Reads local Codex usage data from `%USERPROFILE%\.codex\sessions\**\*.jsonl`
- Session JSONL files are the only supported Codex source; there is no `logs_2.sqlite` fallback
- If no session JSONL file contains rate-limit payloads yet, Codex shows unavailable
- Displays the used percentage for Codex in both the widget and tooltip
- Converts local `remaining_percent` payloads to used percentage before display when needed
- Respects `CODEX_HOME` when it resolves to a Windows-readable path, including WSL-style `/mnt/c/...` paths

## `CODEX_HOME` Notes

- If `CODEX_HOME` is not set, the plugin uses `%USERPROFILE%\.codex`
- Set `CODEX_HOME` if your Codex state lives somewhere else
- The plugin reads the `sessions\**\*.jsonl` tree under that directory
- Windows path example: `C:\Users\<user>\.codex`
- WSL path example: `/mnt/c/Users/<user>/.codex`
- Linux-only paths such as `/home/<user>/.codex` are not readable from Windows TrafficMonitor

## Claude Helper Files

- Dedicated browser profile: `%LOCALAPPDATA%\trafficmonitor-claude-usage-plugin\claude-browser-profile`
- Usage snapshot: `%LOCALAPPDATA%\trafficmonitor-claude-usage-plugin\claude-web-usage.json`
- Helper status: `%LOCALAPPDATA%\trafficmonitor-claude-usage-plugin\claude-web-helper-status.json`
- Watch lock: `%LOCALAPPDATA%\trafficmonitor-claude-usage-plugin\claude-web-helper-watch.lock`

## Claude Helper Prerequisites

- Windows
- Node.js 22 or newer
- Microsoft Edge or Google Chrome installed locally

## Claude Helper Commands

From the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\claude-web-helper.ps1 login
```

- Opens a browser window with the helper's dedicated local profile
- Sign in to Claude there, then close that helper browser window
- In a deployed TrafficMonitor install, the bundled script path is `.\plugins\ClaudeUsagePlugin\claude-web-helper.ps1`

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\claude-web-helper.ps1 start
```

- Launches the background refresh loop as a hidden background process
- This is the normal steady-state mode after the one-time login
- If a watcher is already running, it prints the current watcher PID instead of starting a duplicate

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\claude-web-helper.ps1 status
```

- Shows the latest helper files, watch lock state, and helper process information

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\claude-web-helper.ps1 stop
```

- Stops the running helper watcher and cleans up a stale watch lock when possible

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\claude-web-helper.ps1 watch
```

- Repeats the cookie-based web fetch every 60 seconds in the foreground
- Useful only when you want console output for each refresh attempt

## Operational Notes

- `login` is the only interactive step
- If `claude-web-helper.ps1` plus `helper\claude-web-helper\...` are bundled under `plugins\ClaudeUsagePlugin`, the plugin can auto-start the helper watcher on plugin load
- Close the helper browser window before `start` or `watch`, otherwise the Chromium cookies database may stay locked
- The helper decrypts Chromium cookies under the same Windows user that completed the helper login
- The helper uses only Node built-ins under `helper\claude-web-helper`; no Playwright install is required
- If helper auth expires, the helper status file records the last failure and Claude becomes unavailable after the freshness window expires

## Refresh Behavior

- Claude helper fresh TTL: 90 seconds
- Claude plugin refresh: 30 seconds
- Claude helper watch refresh: 60 seconds
- Codex success refresh: 60 seconds
- Codex failure retry: 5 seconds
