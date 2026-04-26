# Release Checklist

Use this checklist whenever you publish a new private GitHub release for this plugin.

## Before tagging

1. Confirm the repo is clean except for intentional release files.
2. Update [CHANGELOG.md](../CHANGELOG.md) with the user-visible changes.
3. Verify [README.md](../README.md) still matches the current install flow and screenshots.
4. Confirm the release version is consistent in:
   - `src/ClaudeUsagePlugin/ClaudeUsagePlugin.cpp` `TMI_VERSION`
   - [CHANGELOG.md](../CHANGELOG.md)
   - the git tag
   - release note title
   - zip asset names
5. Review [LICENSE](../LICENSE), [NOTICE.md](../NOTICE.md), and [PRIVACY.md](../PRIVACY.md) for public-release readiness.
6. Build both targets:
   - `Release|x64`
   - `Release|Win32`
7. Confirm the build outputs include:
   - `ClaudeUsagePlugin.dll`
   - `ClaudeUsagePlugin\claude-web-helper.ps1`
   - `ClaudeUsagePlugin\helper\claude-web-helper\...`

## Package assets

Create one zip per architecture from the built `plugins` output.

- Recommended asset names:
  - `TrafficMonitorAIUsageLimits_v<version>_x64.zip`
  - `TrafficMonitorAIUsageLimits_v<version>_x86.zip`

Each zip should contain this layout:

```text
plugins
├─ ClaudeUsagePlugin.dll
└─ ClaudeUsagePlugin
   ├─ claude-web-helper.ps1
   └─ helper
      └─ claude-web-helper
         ├─ index.mjs
         ├─ package.json
         └─ package-lock.json
```

## Release page

1. Create a tag such as `v<version>`.
2. Use [docs/release-notes-template.md](release-notes-template.md) as the base release note.
3. Attach newly built `x64` and `x86` zip assets from this exact source revision.
4. Keep the repository private.
5. Do not claim official Anthropic or OpenAI support.

Do not attach or rename older release ZIPs for a newer tag. If the plugin DLL
reports `0.3.10`, the tag, changelog entry, release title, and asset names must
also use `v0.3.10`.

## Final smoke check

1. Test the `x64` asset with an official `x64` TrafficMonitor install.
2. Confirm TrafficMonitor loads `AI Usage Limits`.
3. Confirm `Claude 5h`, `Claude 7d`, `Codex 5h`, and `Codex 7d` appear in display settings.
4. Confirm the tooltip shows localized reset times.
5. Confirm Claude becomes unavailable if the helper snapshot is stale instead of showing stale values forever.
