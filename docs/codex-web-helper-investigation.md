# Codex Web Helper Investigation

Issue: https://github.com/bemaru/trafficmonitor-ai-usage-plugin/issues/7

## Current State

Codex usage limits are currently read from local Codex session JSONL files:

- `%USERPROFILE%\.codex\sessions\**\*.jsonl`
- `%CODEX_HOME%\sessions\**\*.jsonl` when Windows `CODEX_HOME` is set

This keeps the plugin local-file based and avoids handling OpenAI credentials.
The tradeoff is that the plugin only sees Codex homes that are readable from the
Windows TrafficMonitor process and only updates after Codex writes a fresh
rate-limit payload.

## Candidate Web Helper Model

The Claude helper is not an official public API integration. It uses a dedicated
local browser profile, lets the user sign in directly, reads cookies from that
profile, calls Claude's web usage endpoint, and writes a local JSON snapshot.

A Codex web helper would need to follow the same boundary:

1. Open Edge or Chrome with a dedicated local helper profile.
2. Let the user sign in to the OpenAI-owned Codex web surface directly.
3. Read cookies only from that dedicated helper profile.
4. Fetch the Codex usage-limit JSON payload used by the web UI.
5. Write a local snapshot for the TrafficMonitor plugin.

## Security Boundaries

The helper must not:

- read the user's normal browser profile
- reuse Codex CLI OAuth credentials
- reuse stored OpenAI API keys
- ask the user to paste credentials or API keys
- upload usage data, cookies, session JSONL, or helper snapshots

The helper may:

- create a dedicated browser profile under `%LOCALAPPDATA%`
- read cookies from that dedicated profile after the user signs in there
- make cookie-authenticated requests only to OpenAI-owned web origins required
  for the Codex web UI
- write local status and usage snapshot files under `%LOCALAPPDATA%`

## Questions To Resolve

- Which origin owns the relevant Codex usage UI: `chatgpt.com`, an OpenAI
  subdomain, or another OpenAI-owned surface?
- Is the usage-limit data available as JSON without scraping HTML or DOM state?
- Does the payload include both 5h and weekly usage, percentage semantics, and
  reset timestamps?
- How is workspace or organization selection represented?
- What failure states map cleanly to user-facing status text?

## Implementation Gate

Do not replace the current session JSONL source until the Codex web payload is
confirmed stable enough for a helper. The likely order is:

1. Add multi-home JSONL support for Windows/WSL setups.
2. Build a Codex web helper proof of concept on this branch.
3. Decide whether the helper should be an optional fallback source.

Until the web payload is confirmed, this branch should not ship a user-facing
Codex helper in a release package.
