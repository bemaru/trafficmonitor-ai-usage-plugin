# Runtime and Helper Guide

## Runtime Model

Claude usage limits:

- Reads a fresh helper snapshot from `%LOCALAPPDATA%\trafficmonitor-claude-usage-plugin\claude-web-usage.json`
- The helper signs in through its own local Edge or Chrome profile, reads the saved Claude cookies from that profile, and calls `https://claude.ai/api/organizations/{lastActiveOrg}/usage`
- If the helper snapshot is missing or stale, Claude shows unavailable instead of falling back to stale values

Codex usage limits:

- Prefers `%LOCALAPPDATA%\trafficmonitor-claude-usage-plugin\codex-web-usage.json` when valid and no older than 180 seconds
- Falls back to local Codex usage data from `%USERPROFILE%\.codex\sessions\**\*.jsonl` when the web snapshot is missing, invalid, or stale; there is no `logs_2.sqlite` fallback
- Shows unavailable when neither source provides a usable window, including windows whose reset timestamp has passed
- Displays the used percentage for Codex in both the widget and tooltip
- Converts local `remaining_percent` payloads to used percentage before display when needed
- Respects `CODEX_HOME` when it resolves to a Windows-readable path, including WSL-style `/mnt/c/...` paths

## `CODEX_HOME` Notes

- These paths affect the JSONL fallback only. The independent Codex web helper does not use `CODEX_HOME` or CLI credentials.
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

## Privacy Notes

- The Claude helper profile contains Claude browser cookies for the dedicated helper login
- The Claude helper snapshot and status files can contain usage data, organization identifiers or names, file paths, and error text
- Codex session JSONL files can contain sensitive session content; the plugin scans them locally for rate-limit payloads
- The independent Codex helper uses its own Chrome profile; session tokens stay inside Chrome memory and only normalized usage data reaches its JSON snapshot
- Do not share helper files, helper browser profiles, or Codex session JSONL files without reviewing and sanitizing them first

See [../PRIVACY.md](../PRIVACY.md) for the full local-data disclosure.

## Refresh Behavior

- Claude helper fresh TTL: 90 seconds
- Claude plugin refresh: 30 seconds
- Claude helper watch refresh: 60 seconds
- Codex success refresh: 60 seconds
- Codex failure retry: 5 seconds
- Codex helper watch refresh: 60 seconds
- Codex web snapshot fresh TTL: 180 seconds

## Independent Codex Web Helper

Requirements: Windows, Node.js 22 or newer, and Google Chrome. This helper
reuses the PoC's dedicated `codex-browser-profile` path and remains independent
of the Claude helper, including its PowerShell 5.1 DPAPI implementation.

Run these commands from the repository root. In a deployed install, replace
`.\scripts\codex-web-helper.ps1` with
`.\plugins\ClaudeUsagePlugin\codex-web-helper.ps1`.

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\codex-web-helper.ps1 login
```

Sign in directly in the opened Chrome window, then close that window before
running background refresh. No token needs to be copied. Use the ChatGPT
account and workspace whose Codex usage you want to monitor.

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\codex-web-helper.ps1 once
powershell -ExecutionPolicy Bypass -File .\scripts\codex-web-helper.ps1 start
powershell -ExecutionPolicy Bypass -File .\scripts\codex-web-helper.ps1 status
powershell -ExecutionPolicy Bypass -File .\scripts\codex-web-helper.ps1 stop
```

- `once` performs one refresh and exits with failure when usable usage data cannot be fetched.
- `start` launches a hidden watcher that refreshes immediately and then every 60 seconds; repeated starts reuse the existing watcher.
- `status` reports helper state, watcher identity, and snapshot freshness without showing credentials.
- `stop` stops only the Codex watcher. It preserves the dedicated login profile for later use.
- `watch` runs the same refresh loop in the foreground for troubleshooting.
- `reset` stops the Codex watcher and removes only its dedicated profile and helper files; close its interactive Chrome window first.

The helper's application data files are:

- Profile: `%LOCALAPPDATA%\trafficmonitor-claude-usage-plugin\codex-browser-profile`
- Usage: `%LOCALAPPDATA%\trafficmonitor-claude-usage-plugin\codex-web-usage.json`
- Status: `%LOCALAPPDATA%\trafficmonitor-claude-usage-plugin\codex-web-helper-status.json`
- Watch lock: `%LOCALAPPDATA%\trafficmonitor-claude-usage-plugin\codex-web-helper-watch.lock`
- Operation lock: `%LOCALAPPDATA%\trafficmonitor-claude-usage-plugin\codex-web-helper-run.lock`
- Stop request: `%LOCALAPPDATA%\trafficmonitor-claude-usage-plugin\codex-web-helper-stop.json` (temporary process identity metadata)

Chrome handles login cookies. Through a private debugging pipe the helper runs
same-origin requests in Chrome: `/api/auth/session`, then
`/backend-api/wham/usage` with the session access token as a Bearer token.
Only allowlisted usage fields cross the pipe. The helper does not export
cookies, save tokens, or inspect the normal Chrome profile.

An expired session, challenge, failed HTTP request, or invalid usage payload
removes the web snapshot immediately. The plugin then uses usable JSONL data
or displays `--`; it does not preserve expired web values. Closing a watcher
abruptly is also covered by the plugin's 180-second freshness check.

## Codex Watcher Startup Lifecycle

With the bundled files under `plugins\ClaudeUsagePlugin`, the plugin attempts
to start the Codex watcher once when loaded, using the same lifecycle as the
Claude helper. It starts a hidden `codex-web-helper.ps1 start` process; Node's
exclusive lock prevents a duplicate watcher. Startup is skipped until the
dedicated Chrome profile has `Local State` and either
`Default\Network\Cookies` or the legacy `Default\Cookies` file.
The plugin checks only whether those files exist; it never reads their contents.

For initial setup, run `login`, complete sign-in, close its Chrome window,
then restart TrafficMonitor. Subsequent plugin loads reuse Chrome's saved
login automatically. If a watcher is running when you need to sign in again,
run `stop` before `login`.

Manual `start` remains available to resume updates without reloading the
plugin. `stop` leaves the watcher stopped for the current plugin instance;
the plugin does not restart it on every refresh. Loading the plugin again
starts it again if the dedicated profile is initialized. This does not add a
scheduled task, Windows service, or separate Windows startup entry. If you
already start TrafficMonitor with Windows, its normal plugin load performs
the same helper startup.

The endpoints are private ChatGPT web interfaces and can change. Live
authenticated operation was verified locally in the dedicated profile; see
[codex-web-helper-investigation.md](codex-web-helper-investigation.md) for the
verification record and known limitations.
