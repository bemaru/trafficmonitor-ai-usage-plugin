# Troubleshooting

## Quick Verification

After installation or setup, check the following:

1. TrafficMonitor plug-in management shows `AI Usage Limits`
2. Display settings lists `Claude 5h`, `Claude 7d`, `Codex 5h`, and `Codex 7d`
3. The taskbar items show percentages instead of `--`
4. The tooltip shows reset timing for any source that exposes reset metadata
5. If Claude helper is enabled, `%LOCALAPPDATA%\trafficmonitor-claude-usage-plugin\claude-web-usage.json` updates after a successful helper fetch

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

### Claude helper status shows `rate_limited` or `request_failed`

The helper could not fetch `claude.ai` usage right now.
The plugin keeps only the recent helper snapshot within the normal freshness window, then Claude becomes unavailable.

### Claude usage limits do not match the Claude web dashboard

Claude uses the helper snapshot as its Claude source.
Verify that the helper is updating `%LOCALAPPDATA%\trafficmonitor-claude-usage-plugin\claude-web-usage.json` and compare that file to the Claude web dashboard.

### Codex usage does not appear

Check these first:

- `CODEX_HOME` or `%USERPROFILE%\.codex` resolves from the Windows TrafficMonitor process
- The resolved path is Windows-readable
- Codex has written recent local rate-limit data

### `Codex config directory not found`

`CODEX_HOME` or `%USERPROFILE%\.codex` could not be resolved from the Windows TrafficMonitor process.

### `Codex logs_2.sqlite unavailable`

The local Codex SQLite store exists but could not be read.

### `Codex sessions JSONL returned no rate limits`

Codex local session logs were found, but no rate-limit payload was present yet.

### Plug-in loads but the items do not appear

This is usually one of these:

- DLL architecture does not match TrafficMonitor architecture
- The items are not enabled yet in `Display Settings...`

## Constraints

- The Claude web helper depends on an interactive Claude web login stored in its dedicated local Chromium profile
- Claude values depend on a fresh helper snapshot
- The Claude web helper is not a separate installer or Windows service; it is shipped as bundled files under `plugins\ClaudeUsagePlugin`
- Codex usage currently comes from local Codex state, not an official OpenAI usage API
- Codex values update only after Codex itself writes fresh local rate-limit data locally
- This is a best-effort integration surface, not an official Anthropic or OpenAI plugin
