#!/usr/bin/env bash
set -euo pipefail

resolve_home() {
  getent passwd "$(id -u)" | cut -d: -f6
}

resolve_cache_dir() {
  if [ -n "${TRAFFICMONITOR_CLAUDE_CACHE_DIR:-}" ]; then
    printf '%s\n' "$TRAFFICMONITOR_CLAUDE_CACHE_DIR"
    return
  fi

  local windows_localappdata=""
  if command -v cmd.exe >/dev/null 2>&1 && command -v wslpath >/dev/null 2>&1; then
    windows_localappdata="$(cmd.exe /d /c echo %LOCALAPPDATA% 2>/dev/null | tr -d '\r')"
    if [ -n "$windows_localappdata" ]; then
      wslpath "${windows_localappdata}\\trafficmonitor-claude-usage-plugin"
      return
    fi
  fi

  local actual_home
  actual_home="$(resolve_home)"
  printf '%s\n' "${actual_home}/.cache/trafficmonitor-claude-usage-plugin"
}

write_cache() {
  local input_json="$1"
  local cache_dir
  cache_dir="$(resolve_cache_dir)"
  local cache_path="${cache_dir}/claude-statusline.json"

  CACHE_PATH="$cache_path" INPUT_JSON="$input_json" python3 - <<'PY'
import datetime
import json
import os

cache_path = os.environ["CACHE_PATH"]
raw = os.environ.get("INPUT_JSON", "")

try:
    payload = json.loads(raw)
except Exception:
    sys.exit(0)

rate_limits = payload.get("rate_limits")
if not isinstance(rate_limits, dict):
    sys.exit(0)

cache = {}
for key in ("five_hour", "seven_day"):
    source = rate_limits.get(key)
    if not isinstance(source, dict):
        continue

    target = {}
    used = source.get("used_percentage")
    if used is not None:
        target["utilization"] = float(used)

    reset_at = source.get("resets_at")
    if reset_at is not None:
        if isinstance(reset_at, (int, float)):
            dt = datetime.datetime.fromtimestamp(reset_at, tz=datetime.timezone.utc)
            target["resets_at"] = dt.isoformat().replace("+00:00", "Z")
        else:
            target["resets_at"] = str(reset_at)

    if target:
        cache[key] = target

if not cache:
    sys.exit(0)

cache["updated_at"] = datetime.datetime.now(datetime.timezone.utc).isoformat().replace("+00:00", "Z")
os.makedirs(os.path.dirname(cache_path), exist_ok=True)
with open(cache_path, "w", encoding="utf-8") as handle:
    json.dump(cache, handle)
PY
}

input_json="$(cat)"
write_cache "$input_json"
printf '%s' "$input_json" | npx -y ccstatusline@2.2.7
