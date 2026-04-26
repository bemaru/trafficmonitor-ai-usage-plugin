# TrafficMonitor AI Usage Limits v<version>

## Summary

Taskbar usage limits for Claude and Codex through TrafficMonitor.

## What Changed

- Claude now reads a bundled web-helper snapshot as its live source.
- Codex usage stays available from local Codex state in the same DLL.
- The bundled helper layout ships under `plugins\ClaudeUsagePlugin\...`.
- Reset times in tooltips follow the Windows user locale.
- Documentation and install flow were cleaned up for the combined Claude/Codex plugin.

## Assets

- `TrafficMonitorAIUsageLimits_v<version>_x64.zip`
- `TrafficMonitorAIUsageLimits_v<version>_x86.zip`

Pick the asset that matches the architecture of the installed TrafficMonitor build.
The `<version>` value must match the plugin version reported by `AI Usage Limits`
inside TrafficMonitor.

## Notes

- The plugin still deploys as `ClaudeUsagePlugin.dll` for compatibility with TrafficMonitor's plugin layout.
- Claude needs a one-time helper login through `claude-web-helper.ps1 login`.
- Codex reads local state from `%USERPROFILE%\.codex` unless `CODEX_HOME` is set.

## Known Constraints

- Claude values require a fresh helper snapshot.
- Codex values update only after Codex writes fresh local rate-limit data.
- This is a best-effort integration surface, not an official Anthropic or OpenAI plugin.
- Review the repository license, notices, and privacy disclosure before making a public release.
