# TrafficMonitor AI Usage Limits v0.3.10

## Summary

Taskbar usage limits for Claude and Codex through TrafficMonitor.

This release stabilizes the combined Claude/Codex plugin behavior and prepares the project for public distribution.

## What Changed

- Claude now uses the bundled web helper snapshot as its live usage source.
- Codex now reads local session JSONL files as its only usage source.
- Claude and Codex taskbar items now consistently show used percentages.
- Codex session selection now follows the newest rate-limit event timestamp.
- Active Codex session JSONL files can be read while Codex is still writing them.
- Embedded `rate_limits` text from tool output logs is ignored.
- README screenshots, install docs, runtime docs, and release docs were refreshed.
- GitHub Sponsors metadata was added.
- Project license, upstream notice, and privacy/local-data disclosure documents were finalized for public release.

## Assets

- `TrafficMonitorAIUsageLimits_v0.3.10_x64.zip`
- `TrafficMonitorAIUsageLimits_v0.3.10_x86.zip`

Pick the asset that matches the architecture of the installed TrafficMonitor build.

## Notes

- The plugin still deploys as `ClaudeUsagePlugin.dll` for compatibility with TrafficMonitor's plugin layout.
- Claude needs a one-time helper login through `claude-web-helper.ps1 login`.
- Codex reads local session JSONL files from `%USERPROFILE%\.codex` unless `CODEX_HOME` is set.
- This is a best-effort integration surface, not an official Anthropic or OpenAI plugin.

## Known Constraints

- Claude values require a fresh helper snapshot.
- Codex values update only after Codex writes fresh local rate-limit data.
- TrafficMonitor itself is not bundled by this release.
