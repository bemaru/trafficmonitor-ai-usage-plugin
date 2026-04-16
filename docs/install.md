# Install Guide

[TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor) is a Windows system monitor that can show network speed, CPU, and memory in a floating window or directly in the taskbar.
This repository ships only the plugin DLL and bundled helper files. TrafficMonitor itself must be installed separately.

If you do not need TrafficMonitor's temperature monitoring features, the official Lite release is usually enough.

## 1. Match the architecture

Use the plugin DLL that matches the installed TrafficMonitor architecture:

- `x64` plugin for `x64` TrafficMonitor
- `x86` plugin for `x86` TrafficMonitor

## 2. Copy the plugin files

Copy the release contents into the TrafficMonitor `plugins` directory:

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

## 3. Restart TrafficMonitor

After copying the files, restart TrafficMonitor.

## 4. Show the taskbar window

Start TrafficMonitor. You should see its floating monitor window and tray icon.

<p align="center">
  <img src="images/trafficmonitor-tray-icon.png" alt="TrafficMonitor floating window and tray icon" />
</p>

Right-click the TrafficMonitor tray icon or floating window, then choose `Show Taskbar Window`.

<p align="center">
  <img src="images/trafficmonitor-tray-menu-show-taskbar.png" alt="TrafficMonitor tray context menu with Show Taskbar Window" />
</p>

## 5. Enable the plugin items

Right-click the TrafficMonitor taskbar widget, then choose `Display Settings...`.

<p align="center">
  <img src="images/trafficmonitor-taskbar-menu-display-settings.png" alt="TrafficMonitor taskbar context menu with Display Settings" />
</p>

Check these items, then click `OK`:

- `Claude 5h`
- `Claude 7d`
- `Codex 5h`
- `Codex 7d`

<p align="center">
  <img src="images/trafficmonitor-display-settings.png" alt="TrafficMonitor Display settings with Claude and Codex usage items enabled" />
</p>

The taskbar widget should now show the Claude and Codex usage bars.

<p align="center">
  <img src="images/trafficmonitor-taskbar-compact.png" alt="TrafficMonitor taskbar widget showing Claude and Codex usage bars" />
</p>

## 6. Claude one-time login

If you want live Claude values, run:

```powershell
powershell -ExecutionPolicy Bypass -File .\plugins\ClaudeUsagePlugin\claude-web-helper.ps1 login
```

That opens a browser window with the helper's dedicated profile. Sign in to Claude there, then close that helper browser window.

## 7. Codex path override

If your Codex state does not live in `%USERPROFILE%\.codex`, set `CODEX_HOME` in the Windows environment before launching TrafficMonitor.

## Quick Verification

- TrafficMonitor plug-in management shows `AI Usage Limits`
- Display settings lists `Claude 5h`, `Claude 7d`, `Codex 5h`, and `Codex 7d`
- The taskbar items show percentages instead of `--`
- The tooltip shows reset timing when the active source exposes it
