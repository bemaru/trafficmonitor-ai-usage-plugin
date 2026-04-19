# Troubleshooting

## Quick Verification

After installation or setup, check the following:

1. TrafficMonitor plug-in management shows `AI Usage Limits`
2. Display settings lists `Claude 5h`, `Claude 7d`, `Codex 5h`, and `Codex 7d`
3. The taskbar items show used percentages instead of `--`
4. The tooltip shows used percentages and reset timing for any source that exposes reset metadata
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
- At least one `sessions\**\*.jsonl` file exists under that directory
- Codex has written recent session JSONL rate-limit data

### Codex percentage looks inverted from the Codex usage page

TrafficMonitor now standardizes Codex on used percentage in both the widget and tooltip.
If the Codex usage page is showing remaining percentage for the same window, the two numbers should add up to about 100%.
If the numbers are not simple inverses, verify that `%USERPROFILE%\.codex\sessions\**\*.jsonl` is being updated and that `CODEX_HOME` points at the same Codex profile the dashboard is using.

### `Codex config directory not found`

`CODEX_HOME` or `%USERPROFILE%\.codex` could not be resolved from the Windows TrafficMonitor process.

### `Codex sessions JSONL not found`

No session JSONL files were found under the resolved Codex config directory.
Verify `CODEX_HOME`, Windows path visibility, and that Codex has started at least one session on that profile.

### `Codex sessions JSONL returned no rate limits`

Codex local session logs were found, but no rate-limit payload was present yet.
The plugin does not fall back to another local store, so Codex stays unavailable until a session JSONL entry includes rate-limit data.

### Plug-in loads but the items do not appear

This is usually one of these:

- DLL architecture does not match TrafficMonitor architecture
- The items are not enabled yet in `Display Settings...`

## Constraints

- The Claude web helper depends on an interactive Claude web login stored in its dedicated local Chromium profile
- Claude values depend on a fresh helper snapshot
- The Claude web helper is not a separate installer or Windows service; it is shipped as bundled files under `plugins\ClaudeUsagePlugin`
- Codex usage comes from local Codex session JSONL files, not an official OpenAI usage API
- There is no SQLite fallback for Codex usage
- Codex values update only after Codex itself writes fresh local rate-limit data into session JSONL files
- This is a best-effort integration surface, not an official Anthropic or OpenAI plugin
