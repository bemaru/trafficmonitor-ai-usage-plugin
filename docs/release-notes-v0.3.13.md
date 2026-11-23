# TrafficMonitor AI Usage Limits v0.3.13

## Summary

Patch release that prevents a persisted Claude helper watch lock from
mistaking a reused Windows PID for the helper and terminating an unrelated
process.

## What Changed

- PowerShell and Node now validate the watch process name, command line, and
  start time against the lock metadata.
- `status` and `start` remove stale or unreadable locks without affecting the
  process currently using the recorded PID.
- `stop` terminates only the watcher validated by the current lock instead of
  broadly stopping every Node process with a similar command line.
- The wrapper launches `index.mjs` by absolute path for clearer process
  identity.
- A regression test reproduces PID reuse with a valid-looking unrelated Node
  process and verifies both the PowerShell and Node lock paths.

## Assets

- `TrafficMonitorAIUsageLimits_v0.3.13_x64.zip`
- `TrafficMonitorAIUsageLimits_v0.3.13_x86.zip`
- `SHA256SUMS`

Pick the asset that matches the architecture of the installed TrafficMonitor
build. Each zip includes `LICENSE`, `NOTICE.md`, and `PRIVACY.md` at the root.

## Notes

- The plugin still deploys as `ClaudeUsagePlugin.dll` for compatibility with
  TrafficMonitor's plugin layout.
- Claude needs a one-time helper login through
  `claude-web-helper.ps1 login`.
- Codex reads local session JSONL files from `%USERPROFILE%\.codex` unless
  `CODEX_HOME` is set.
- This is a best-effort integration surface, not an official Anthropic or
  OpenAI plugin.

## Known Constraints

- Claude values require a fresh helper snapshot.
- Codex values update only after Codex writes fresh local rate-limit data.
- TrafficMonitor itself is not bundled by this release.
