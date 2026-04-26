# Notices

This project builds a plugin for [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor).
TrafficMonitor itself is not bundled in this repository.

## TrafficMonitor Interface

- Upstream project: [zhongyang219/TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor)
- Upstream license file: [TrafficMonitor LICENSE](https://github.com/zhongyang219/TrafficMonitor/blob/master/LICENSE)
- Included interface file: `include/PluginInterface.h`
- Local provenance: copied from TrafficMonitor's `include/PluginInterface.h`
- Header notice: `TrafficMonitor` plugin interface, copyright Zhong Yang 2021

Keep the upstream copyright header in `include/PluginInterface.h`.
If the interface is refreshed from upstream, update this notice with the
source commit or tag used for the copy.

The upstream TrafficMonitor license is the Anti-996 License Version 1.0
(Draft). Because this repository includes the upstream plugin interface, do not
publish source or binary releases until the project license and redistribution
terms have been reviewed for compatibility.

## Bundled Helper Runtime

The Claude web helper under `helper/claude-web-helper` uses Node.js built-in
modules only. Its `package-lock.json` currently records no third-party npm
runtime packages.

## Service Names

Claude, Anthropic, Codex, OpenAI, Windows, Microsoft Edge, Google Chrome, and
TrafficMonitor are names of their respective owners. This project is an
unofficial integration and is not endorsed by Anthropic, OpenAI, Microsoft,
Google, or the TrafficMonitor project.
