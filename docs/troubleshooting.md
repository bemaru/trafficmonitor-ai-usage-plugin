# Troubleshooting

## Quick Verification

After installation or setup, check the following:

1. TrafficMonitor plug-in management shows `AI Usage Limits`
2. Display settings lists `Claude 5h`, `Claude 7d`, `Codex 5h`, and `Codex 7d`
3. The taskbar items show used percentages instead of `--`
4. The tooltip shows used percentages and reset timing for any source that exposes reset metadata
5. If Claude helper is enabled, `%LOCALAPPDATA%\trafficmonitor-claude-usage-plugin\claude-web-usage.json` updates after a successful helper fetch
6. If Codex web helper is enabled, `codex-web-helper.ps1 status` reports a recent successful snapshot

## Common Issues

### Claude values show `--` or `Claude usage limits unavailable`

Verify that `%LOCALAPPDATA%\trafficmonitor-claude-usage-plugin\claude-web-usage.json` exists and was updated recently by the helper.
The Claude tooltip also surfaces the latest helper status when no fresh helper snapshot is available.

```powershell
powershell -ExecutionPolicy Bypass -File .\plugins\ClaudeUsagePlugin\claude-web-helper.ps1 status
```

That should show a healthy watcher and recent files.

### Claude helper status shows `login_required` or `access_denied`

Run the Claude login again and complete the login in the opened browser window:

```powershell
powershell -ExecutionPolicy Bypass -File .\plugins\ClaudeUsagePlugin\claude-web-helper.ps1 login
```

### Claude helper status shows `profile_in_use`

Close the helper browser window that was opened by `login`, then run `start` again.
Use `watch` only for foreground troubleshooting.

### Claude helper reports an old watcher after Windows restarts

Plugin `v0.3.13` and newer validates the process behind
`claude-web-helper-watch.lock` by name, command line, and start time. If Windows
reused the recorded PID for another process, `status` or `start` removes the
stale lock without terminating that unrelated process.

```powershell
powershell -ExecutionPolicy Bypass -File .\plugins\ClaudeUsagePlugin\claude-web-helper.ps1 status
powershell -ExecutionPolicy Bypass -File .\plugins\ClaudeUsagePlugin\claude-web-helper.ps1 start
```

### Claude helper status shows `rate_limited` or `request_failed`

The helper could not fetch `claude.ai` usage right now.
The plugin keeps only the recent helper snapshot within the normal freshness window, then Claude becomes unavailable.

### Claude usage limits do not match the Claude web dashboard

Claude uses the helper snapshot as its Claude source.
Verify that the helper is updating `%LOCALAPPDATA%\trafficmonitor-claude-usage-plugin\claude-web-usage.json` and compare that file to the Claude web dashboard.

### Codex usage does not appear

Check the independent helper first:

```powershell
powershell -ExecutionPolicy Bypass -File .\plugins\ClaudeUsagePlugin\codex-web-helper.ps1 status
powershell -ExecutionPolicy Bypass -File .\plugins\ClaudeUsagePlugin\codex-web-helper.ps1 once
```

Run `login` again if the dedicated session expired. Complete sign-in directly
in the Chrome window, close it, then run `start`. A missing Chrome install,
profile still in use, access denial, challenge, HTTP failure, or changed
response shape prevents a web snapshot from being used. The helper records
only sanitized state; do not paste authorization headers or session responses
into a bug report.

After the one-time login, restarting TrafficMonitor also starts the bundled
Codex watcher automatically. If `login` says the watcher is running, run
`stop` first. A watcher stopped manually remains stopped in the current
plugin instance; use `start` to resume immediately or reload the plugin.
The plugin attempts startup once per load and skips it before the dedicated
Chrome profile has been initialized. No scheduled task or extra Windows
startup entry is needed.

When using the optional local JSONL fallback, check:

- `CODEX_HOME` or `%USERPROFILE%\.codex` resolves from the Windows TrafficMonitor process
- The resolved path is Windows-readable
- At least one `sessions\**\*.jsonl` file exists under that directory
- Codex has written recent session JSONL rate-limit data

### Codex percentage looks inverted from the Codex usage page

TrafficMonitor now standardizes Codex on used percentage in both the widget and tooltip.
If the Codex usage page is showing remaining percentage for the same window, the two numbers should add up to about 100%.
If the numbers are not simple inverses, check that the dedicated Codex helper
login uses the same ChatGPT account and workspace as the dashboard. If the
plugin has fallen back to JSONL, verify that those session files are being
updated and that `CODEX_HOME` points at the intended Codex profile.

### A weekly Codex limit appears under `X5h`

Current Codex payloads can place a seven-day limit in `primary` instead of `secondary`.
Plugin `v0.3.12` and newer classify limits by their reported duration:

- `300` minutes is displayed as `X5h`
- `10080` minutes is displayed as `X7d`

If a current payload is still shown under the wrong label, verify that TrafficMonitor loaded `v0.3.12` or newer and restart TrafficMonitor after replacing the DLL.

### Codex values jump between different weekly reset times

`CODEX_HOME` controls which Codex profile directory TrafficMonitor reads, but multiple session JSONL files inside the same profile can still be active at the same time.
If those sessions write conflicting rate-limit windows, the plugin prefers the newest observed reset window from recent events instead of letting an older reset window overwrite it only because that session wrote a token-count event later.

### `Codex config directory not found`

`CODEX_HOME` or `%USERPROFILE%\.codex` could not be resolved from the Windows TrafficMonitor process.

### `Codex sessions JSONL not found`

No session JSONL files were found under the resolved Codex config directory.
Verify `CODEX_HOME`, Windows path visibility, and that Codex has started at least one session on that profile.

### `Codex sessions JSONL returned no rate limits`

Codex local session logs were found, but no usable rate-limit payload was
present. Use the independent Codex helper or wait for a session JSONL entry
with current rate-limit data. There is no other local-store fallback.

### Codex web values disappear after an error

This is intentional: a failed helper refresh removes the previous web snapshot.
Snapshots older than 180 seconds and windows past their reset time are also
rejected. The plugin uses usable JSONL data or displays `--`. Repeat `login`
for an expired session; use `status` to inspect failures. For a challenge,
open the dedicated interactive login window and complete the site's normal
verification before closing it and restarting the watcher.

### Codex helper reports that the profile is in use

Close the Chrome window opened by `codex-web-helper.ps1 login`. Do not run
multiple `once`/`watch` processes against the same dedicated profile. Run
`stop` before changing helper modes. The helper must not attach to the normal
Chrome profile or use a remotely exposed debugging port.

### Plug-in loads but the items do not appear

This is usually one of these:

- DLL architecture does not match TrafficMonitor architecture
- The items are not enabled yet in `Display Settings...`

## Constraints

- The Claude web helper depends on an interactive Claude web login stored in its dedicated local Chromium profile
- Claude values depend on a fresh helper snapshot
- The Claude web helper is not a separate installer or Windows service; it is shipped as bundled files under `plugins\ClaudeUsagePlugin`
- Codex web usage depends on a dedicated interactive ChatGPT login and private web endpoints, which may change
- Codex helper requires Google Chrome and Node.js 22+; no Codex CLI session or copied token is required
- There is no SQLite fallback for Codex usage
- JSONL fallback values update only after Codex writes fresh local rate-limit data into session JSONL files
- This is a best-effort integration surface, not an official Anthropic or OpenAI plugin
