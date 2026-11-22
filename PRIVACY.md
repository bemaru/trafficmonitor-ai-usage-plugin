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

To remove only Claude login data, stop the Claude watcher, close its helper
browser, then delete `claude-browser-profile` under
`%LOCALAPPDATA%\trafficmonitor-claude-usage-plugin`. Deleting the entire shared
directory removes both Claude and Codex helper data; stop both watchers first.

## Codex Session Files

When no fresh Codex web snapshot is available, Codex values can be read from
local Codex session JSONL files:

- Default path: `%USERPROFILE%\.codex\sessions\**\*.jsonl`
- Override path: `CODEX_HOME\sessions\**\*.jsonl`

Codex session files can contain sensitive session content, prompts, outputs,
local paths, model metadata, tool metadata, and rate-limit events. The plugin
opens these files locally and scans for rate-limit payloads. It does not upload
Codex session files.

Do not attach Codex session JSONL files to bug reports or release artifacts
unless they have been sanitized.

## Codex Web Helper

The independent Codex helper uses a dedicated Chrome profile. It does not
require a Codex CLI session, CLI OAuth credentials, an OpenAI API key, or a
manually copied token.

- `codex-web-helper.ps1 login` opens Chrome with a dedicated local
  browser profile at
  `%LOCALAPPDATA%\trafficmonitor-claude-usage-plugin\codex-browser-profile`.
- Chrome stores ChatGPT/OpenAI login cookies in that profile after sign-in,
  as it does for an ordinary browser session. The helper does not copy,
  decrypt, export, or log those cookies.
- Background refresh launches Chrome headlessly with that same profile.
  A private debugging pipe is used instead of a listening debugging port.
- Requests to `https://chatgpt.com/api/auth/session` and
  `https://chatgpt.com/backend-api/wham/usage` execute inside Chrome. The
  session access token is used as a Bearer token only in browser memory.
  Session responses and authorization headers are never returned to Node,
  printed, or written by the helper.
- `codex-web-usage.json` contains only a schema version, source name,
  snapshot timestamp, used percentages, and reset timestamps.
- `codex-web-helper-status.json`, `codex-web-helper-watch.lock`,
  `codex-web-helper-run.lock`, and the
  temporary `codex-web-helper-stop.json` contain
  operational state and process metadata. They contain no raw HTTP response,
  access token, cookie, account email, or credentials.
- A failed refresh removes the previous usage snapshot. The plugin rejects
  snapshots older than 180 seconds and falls back to local JSONL data when
  usable. Expired usage windows are not displayed.
- On plugin load, the plugin can launch the bundled Codex watcher once after
  its dedicated Chrome profile is initialized. It checks only for the
  profile's `Local State` and cookie database file (`Default\Network\Cookies`
  or legacy `Default\Cookies`), without reading their contents. This does not create a scheduled task, Windows service, or
  additional Windows startup entry. The saved login remains in Chrome's
  dedicated profile; no token is copied into plugin startup settings.

All of these files are under
`%LOCALAPPDATA%\trafficmonitor-claude-usage-plugin`. Do not share either helper
browser profile: the profiles contain the browser-managed authenticated
session. To remove only Codex login data, stop the Codex watcher, close its
browser, then delete `codex-browser-profile`. The `reset` command removes the
Codex profile and helper files after stopping its watcher. Do not delete the shared base
directory if you want to keep the Claude helper login.

## Network Behavior

- The Claude helper makes network requests to `https://claude.ai` when fetching
  Claude usage data.
- The plugin reads Codex snapshots and session JSONL locally. The independent
  Codex helper uses Chrome to make authenticated requests to `chatgpt.com`
  every 60 seconds while running. Interactive login can visit OpenAI's login
  service and the identity provider selected by the user.
- TrafficMonitor itself, Windows, Edge, Chrome, Claude, Codex, and related
  services may have their own network behavior outside this plugin.

## Release Review

Review this document before each release against the current runtime behavior,
helper files, and release asset contents.
