# Changelog

## Unreleased

### Changed
- Claude usage now uses only the plugin's own cached API snapshot as the Claude fallback source.

### Fixed
- Claude usage polling now respects `Retry-After` when the Anthropic usage API returns `429 rate limited`, instead of retrying every 5 seconds.
- Claude refresh scheduling now measures the next poll window from the completed request time, which avoids retrying earlier than intended after slow API calls.

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
