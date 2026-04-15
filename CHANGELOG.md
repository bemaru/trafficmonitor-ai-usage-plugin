# Changelog

## Unreleased

### Added
- Added an optional Claude web helper under `helper/claude-web-helper` plus the `scripts/claude-web-helper.ps1` wrapper.
- Added helper wrapper commands for `start`, `status`, and `stop` so the Claude watcher can run in the background and be inspected without manual process hunting.

### Changed
- Claude now reads only a fresh `claude-web-usage.json` helper snapshot for live Claude data; if that snapshot is missing or stale, Claude shows unavailable.
- The Claude web helper now uses a dedicated local browser profile plus direct cookie-based `claude.ai` requests instead of replaying Playwright browser state.
- The helper wrapper now reports the watch lock, helper status, and current snapshot files so local troubleshooting is simpler.

### Fixed
- Claude no longer keeps stale local fallback values indefinitely when the live source is unavailable.
- The Claude web helper now keeps the most recent successful helper snapshot across transient `request_failed` fetch errors, while the DLL still expires that snapshot after the existing 90-second freshness window.
- The Claude web helper now uses the current `lastActiveOrg` cookie to call `GET /api/organizations/{orgId}/usage` directly before falling back to the broader organizations lookup, avoiding the `GET /api/organizations` 500 path that could leave Claude unavailable even with a valid signed-in web session.
- Claude runtime now uses the helper snapshot as its only live Claude source, with a simpler `helper -> short fresh snapshot -> unavailable` model instead of OAuth/statusline/plugin-cache fallbacks.
- Removed the remaining legacy OAuth/statusline/plugin-cache Claude runtime code paths from the DLL after the helper-only runtime simplification.

## 0.3.7 - 2026-04-15

### Changed
- Claude tooltip errors now include the concrete HTTP status for denied and rate-limited API responses.

### Fixed
- Claude cached fallback responses no longer mark API failures as a successful refresh, so auth and rate-limit states recover on the shorter retry cadence.

## 0.3.6 - 2026-04-15

### Changed
- Claude now tries the OAuth usage endpoint first and only falls back to the freshest available local snapshot when the live request is unavailable.

### Fixed
- Fresh Claude statusline cache no longer masks newer live API data when Claude Code is not actively updating the local bridge cache.

## 0.3.5 - 2026-04-15

### Changed
- Shifted the `7d` bars further toward muted gray-blue and gray-green tones so they remain visibly secondary to `5h` in the compact taskbar UI.

## 0.3.4 - 2026-04-15

### Changed
- Muted the `7d` bar colors for Claude and Codex so `5h` reads as the primary usage signal in the compact taskbar layout.

## 0.3.3 - 2026-04-15

### Changed
- Increased the visual separation between `5h` and `7d` bar colors while keeping a single color family per provider.

### Fixed
- Usage bar layout now reserves a realistic fixed value-text width (`99.9%`), so bars stay stable without becoming unnecessarily short.

## 0.3.2 - 2026-04-15

### Fixed
- Usage bar width now reserves a fixed value-text area, so bar length no longer changes with `9%` vs `10%` style digit-count differences.

## 0.3.1 - 2026-04-15

### Changed
- Claude usage now uses only the plugin's own cached API snapshot as the Claude fallback source.
- Claude now prefers an official Claude Code statusline bridge cache when configured, with the OAuth usage endpoint retained as fallback.
- Claude local cache and statusline bridge files now live under `trafficmonitor-claude-usage-plugin`, with backward-compatible reads from the previous `trafficmonitor-ai-usage-plugin` path.
- Added a WSL Claude Code statusline wrapper path so WSL sessions can write the Windows-readable Claude bridge cache directly.
- Tooltips no longer expose internal `Source:` labels.
- Claude and Codex usage bars now keep one color family per provider, with a slightly stronger tone for `5h` than `7d`.

### Fixed
- Claude usage polling now respects `Retry-After` when the Anthropic usage API returns `429 rate limited`, instead of retrying every 5 seconds.
- Claude refresh scheduling now measures the next poll window from the completed request time, which avoids retrying earlier than intended after slow API calls.
- Claude and Codex reset timestamps in tooltips now follow the Windows user locale date/time format instead of a fixed `YYYY-MM-DD HH:MM` string.
- Fresh Claude statusline bridge cache can now be picked up immediately even while the OAuth API is still in `Retry-After` backoff.
- Fresh Claude statusline bridge cache is now re-read every 5 seconds instead of waiting for the 180 second OAuth success interval.
- Codex tooltip reset timestamps now parse the current local `reset_at` field emitted by Codex websocket rate-limit events.

## 0.3.0 - 2026-04-15

### Added
- Added `Codex 5h` and `Codex 7d` items to the same plugin DLL.
- Added tooltip reset timing for Claude and Codex when reset metadata is available.

### Changed
- README now documents the combined Claude/Codex behavior and local Codex data sources.

### Fixed
- Codex snapshot reads now use the same guarded snapshot pattern as Claude.
- Failed refreshes retry after 5 seconds instead of waiting a full minute.

## 0.2.0 - 2026-04-15

### Added
- Extracted `ClaudeUsagePlugin` into a standalone private repository.
- Documented the standalone build layout and install flow for `ClaudeUsagePlugin.dll`.

### Changed
- Reworked the README for the standalone repo and DLL-only delivery model.

### Fixed
- Refresh failures now fall back to `unavailable` instead of keeping stale values.
