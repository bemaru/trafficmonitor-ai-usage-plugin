# TrafficMonitor AI Usage Limits v0.3.11

## Summary

Patch release for Codex usage stability when multiple local session JSONL files
report conflicting rate-limit reset windows.

## What Changed

- Codex metric selection now prefers the newest observed reset window from recent
  session events, using event timestamp only as a tie-breaker.
- This prevents older weekly reset windows from overwriting a newly observed
  Codex reset window when long-running sessions keep writing old payloads.
- Troubleshooting docs now describe why this can happen even when `CODEX_HOME`
  points at the correct Codex profile.
- README quick-start asset names were updated for `v0.3.11`.

## Assets

- `TrafficMonitorAIUsageLimits_v0.3.11_x64.zip`
- `TrafficMonitorAIUsageLimits_v0.3.11_x86.zip`

Pick the asset that matches the architecture of the installed TrafficMonitor build.
Each zip includes `LICENSE`, `NOTICE.md`, and `PRIVACY.md` at the root.

## Notes

- The plugin still deploys as `ClaudeUsagePlugin.dll` for compatibility with TrafficMonitor's plugin layout.
- Claude needs a one-time helper login through `claude-web-helper.ps1 login`.
- Codex reads local session JSONL files from `%USERPROFILE%\.codex` unless `CODEX_HOME` is set.
- This is a best-effort integration surface, not an official Anthropic or OpenAI plugin.

## Known Constraints

- Claude values require a fresh helper snapshot.
- Codex values update only after Codex writes fresh local rate-limit data.
- TrafficMonitor itself is not bundled by this release.
