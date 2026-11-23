# TrafficMonitor AI Usage Limits v0.3.12

## Summary

Patch release that fixes Codex five-hour and seven-day limit classification,
reduces repeated session-log work, and keeps compact taskbar drawing inside the
rectangle supplied by TrafficMonitor.

## What Changed

- Codex limits are classified by `window_minutes`: `300` minutes maps to `X5h`
  and `10080` minutes maps to `X7d`.
- A seven-day-only payload delivered in `primary` no longer appears under
  `X5h`.
- Payloads that do not provide `window_minutes` retain the legacy
  `primary` / `secondary` fallback.
- Codex refresh parses the newest active session-file window instead of
  rereading every eligible historical JSONL file.
- Compact taskbar bars and spacing were tightened, and drawing is clipped to
  the host rectangle to avoid overlapping neighboring taskbar content.
- DLL-level regression tests now cover current, legacy, reordered, and unknown
  rate-limit windows on x64 and x86.

## Assets

- `TrafficMonitorAIUsageLimits_v0.3.12_x64.zip`
- `TrafficMonitorAIUsageLimits_v0.3.12_x86.zip`
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
