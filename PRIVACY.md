# Privacy and Local Data

This plugin is a local TrafficMonitor integration. It does not run project
telemetry or send usage data to a service operated by this repository.

## Claude Web Helper

Claude values come from the bundled helper, not from an official public
Anthropic plugin API.

- `claude-web-helper.ps1 login` opens Edge or Chrome with a dedicated local
  browser profile at
  `%LOCALAPPDATA%\trafficmonitor-claude-usage-plugin\claude-browser-profile`.
- That profile stores Claude browser cookies after the user signs in.
- The helper reads and decrypts cookies from that dedicated profile under the
  same Windows user account.
- The helper sends cookie-authenticated requests to `https://claude.ai` for the
  active organization's usage data.
- The helper writes local status and usage snapshot files under
  `%LOCALAPPDATA%\trafficmonitor-claude-usage-plugin`.

Local Claude helper files can include usage-limit data, organization
identifiers or names, local file paths, and troubleshooting error text. Do not
share the helper browser profile, helper status JSON, or helper usage snapshot
unless you have reviewed them first.

To remove the local Claude helper data, stop the helper watcher, close
TrafficMonitor, then delete
`%LOCALAPPDATA%\trafficmonitor-claude-usage-plugin`.

## Codex Session Files

Codex values are read from local Codex session JSONL files:

- Default path: `%USERPROFILE%\.codex\sessions\**\*.jsonl`
- Override path: `CODEX_HOME\sessions\**\*.jsonl`

Codex session files can contain sensitive session content, prompts, outputs,
local paths, model metadata, tool metadata, and rate-limit events. The plugin
opens these files locally and scans for rate-limit payloads. It does not upload
Codex session files.

Do not attach Codex session JSONL files to bug reports or release artifacts
unless they have been sanitized.

## Network Behavior

- The Claude helper makes network requests to `https://claude.ai` when fetching
  Claude usage data.
- The Codex reader is local-file based and does not make network requests.
- TrafficMonitor itself, Windows, Edge, Chrome, Claude, Codex, and related
  services may have their own network behavior outside this plugin.

## Release Review

Review this document before each release against the current runtime behavior,
helper files, and release asset contents.
