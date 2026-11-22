import fsp from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import process from 'node:process';
import net from 'node:net';
import { createHash } from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { closeActiveBrowser, fetchUsageSnapshot, HelperError, isProfileInUse, openLoginBrowser } from './browser.mjs';

export const REFRESH_MS = 60_000;
const SCRIPT_PATH = fileURLToPath(import.meta.url);
const STATES = new Set([
  'ok', 'login_required', 'access_denied', 'rate_limited', 'http_error', 'unexpected_response',
  'cloudflare_blocked', 'request_timeout', 'request_failed', 'account_unavailable', 'usage_unavailable',
  'browser_not_found', 'browser_start_failed', 'browser_closed', 'browser_protocol_failed',
  'browser_timeout', 'browser_evaluation_failed', 'navigation_failed', 'profile_in_use',
  'process_check_failed', 'login_browser_opened', 'stopped', 'storage_failed', 'lock_failed',
  'reset', 'reset_refused',
]);

export function getPaths(baseDir) {
  const base = baseDir || (process.env.LOCALAPPDATA
    ? path.join(process.env.LOCALAPPDATA, 'trafficmonitor-claude-usage-plugin')
    : path.join(os.homedir(), '.cache', 'trafficmonitor-claude-usage-plugin'));
  return {
    base,
    profile: path.join(base, 'codex-browser-profile'),
    usage: path.join(base, 'codex-web-usage.json'),
    status: path.join(base, 'codex-web-helper-status.json'),
    watchLock: path.join(base, 'codex-web-helper-watch.lock'),
    runLock: path.join(base, 'codex-web-helper-run.lock'),
    stop: path.join(base, 'codex-web-helper-stop.json'),
  };
}

async function readJson(filePath) {
  try { return JSON.parse((await fsp.readFile(filePath, 'utf8')).replace(/^\uFEFF/, '')); } catch { return null; }
}

async function removeFile(filePath) {
  try { await fsp.unlink(filePath); } catch (error) { if (error.code !== 'ENOENT') throw error; }
}

async function atomicWriteJson(filePath, value) {
  const temporaryPath = `${filePath}.${process.pid}.tmp`;
  try {
    await fsp.writeFile(temporaryPath, `${JSON.stringify(value, null, 2)}\n`, { encoding: 'utf8', mode: 0o600 });
    await fsp.rename(temporaryPath, filePath);
  } finally {
    await removeFile(temporaryPath);
  }
}

export function safeState(error) {
  return STATES.has(error?.state) ? error.state : 'request_failed';
}

function lockProcessIsRunning(lock) {
  if (!lock || !Number.isSafeInteger(lock.pid) || lock.pid <= 0 || !['watch', 'once', 'login', 'reset'].includes(lock.mode)
    || typeof lock.script_path !== 'string' || path.basename(lock.script_path).toLowerCase() !== 'index.mjs'
    || path.basename(path.dirname(lock.script_path)).toLowerCase() !== 'codex-web-helper'
    || !Number.isFinite(Date.parse(lock.started_at))) return false;
  if (process.platform !== 'win32') {
    try { process.kill(lock.pid, 0); return true; } catch (error) { return error.code === 'EPERM'; }
  }
  const script = [
    '$p = Get-CimInstance Win32_Process -Filter "ProcessId = $env:TM_CODEX_LOCK_PID" -ErrorAction Stop',
    'if (-not $p -or $p.Name -ne "node.exe") { exit 2 }',
    '$started = [DateTimeOffset]::Parse($env:TM_CODEX_LOCK_STARTED).ToUniversalTime()',
    '$created = ([DateTimeOffset]$p.CreationDate).ToUniversalTime()',
    `$scriptPattern = '(?i)(^|\\s)"?' + [regex]::Escape($env:TM_CODEX_LOCK_SCRIPT) + '"?(\\s|$)'`,
    '$valid = $p.CommandLine -and ($p.CommandLine -match $scriptPattern) -and',
    '($p.CommandLine -match ("(?i)(^|\\s)" + $env:TM_CODEX_LOCK_MODE + "(\\s|$)")) -and',
    '([Math]::Abs(($started - $created).TotalSeconds) -le 30)',
    'if ($valid) { exit 0 }; exit 2',
  ].join('\n');
  const result = spawnSync('powershell.exe', ['-NoProfile', '-EncodedCommand', Buffer.from(script, 'utf16le').toString('base64')], {
    windowsHide: true, stdio: 'ignore', timeout: 10_000,
    env: {
      ...process.env, TM_CODEX_LOCK_PID: String(lock.pid), TM_CODEX_LOCK_STARTED: lock.started_at,
      TM_CODEX_LOCK_SCRIPT: lock.script_path, TM_CODEX_LOCK_MODE: lock.mode,
    },
  });
  // A failed process query is not proof that an owner is gone.
  if (result.error || ![0, 2].includes(result.status)) throw new HelperError('process_check_failed');
  return result.status === 0;
}

export async function acquireLock(filePath, mode) {
  await fsp.mkdir(path.dirname(filePath), { recursive: true });
  // A Windows named pipe is the actual exclusive lock. The OS releases it on
  // process death, so stale metadata can never make two reclaimers unlink a
  // newer owner's lock. No requests or credentials are accepted on this pipe.
  const normalizedPath = process.platform === 'win32' ? path.resolve(filePath).toLowerCase() : path.resolve(filePath);
  const hash = createHash('sha256').update(normalizedPath).digest('hex');
  const pipePath = process.platform === 'win32'
    ? `\\\\.\\pipe\\trafficmonitor-codex-${hash}`
    : `\0trafficmonitor-codex-${hash}`;
  const server = net.createServer((socket) => socket.destroy());
  const listening = await new Promise((resolve, reject) => {
    server.once('error', (error) => {
      if (error.code === 'EADDRINUSE') resolve(false);
      else reject(new HelperError('lock_failed'));
    });
    server.listen(pipePath, () => resolve(true));
  });
  if (!listening) return null;
  const close = () => new Promise((resolve) => server.close(() => resolve()));
  try {
    const current = await readJson(filePath);
    // Preserve a live helper from the previous file-lock implementation.
    if (current && current.lock_kind !== 'named-pipe' && lockProcessIsRunning(current)) {
      await close();
      return null;
    }
    const stat = await fsp.stat(filePath).catch(() => null);
    if (!current && stat && Date.now() - stat.mtimeMs < 10_000) {
      await close();
      return null;
    }
    const value = {
      pid: process.pid, mode, started_at: new Date(Date.now() - process.uptime() * 1000).toISOString(),
      refresh_ms: REFRESH_MS, script_path: SCRIPT_PATH, lock_kind: 'named-pipe',
    };
    await atomicWriteJson(filePath, value);
    let released = false;
    return {
      value,
      async release() {
        if (released) return;
        released = true;
        try {
          const latest = await readJson(filePath);
          if (latest?.pid === value.pid && latest.started_at === value.started_at) await removeFile(filePath);
        } finally {
          await close();
        }
      },
    };
  } catch (error) {
    await close();
    throw error;
  }
}

async function writeStatus(paths, state, details = {}, now = Date.now()) {
  const payload = { state: STATES.has(state) ? state : 'request_failed', updated_at: new Date(now).toISOString() };
  if (['session', 'usage'].includes(details.endpoint)) payload.endpoint = details.endpoint;
  if (Number.isInteger(details.http_status) && details.http_status >= 100 && details.http_status <= 599) {
    payload.http_status = details.http_status;
  }
  await atomicWriteJson(paths.status, payload);
}

function sanitizeSnapshot(payload, now) {
  if (!payload || typeof payload !== 'object') return null;
  const snapshot = { schema_version: 1, source: 'codex-web', updated_at: new Date(now).toISOString() };
  for (const [key, duration] of [['five_hour', 18_000_000], ['seven_day', 604_800_000]]) {
    const metric = payload[key];
    const resetAt = typeof metric?.resets_at === 'string' ? Date.parse(metric.resets_at) : NaN;
    if (typeof metric?.utilization !== 'number' || !Number.isFinite(metric.utilization)
      || metric.utilization < 0 || metric.utilization > 100 || !Number.isFinite(resetAt)
      || resetAt <= now || resetAt > now + duration + 60_000) return null;
    snapshot[key] = { utilization: metric.utilization, resets_at: new Date(resetAt).toISOString() };
  }
  return snapshot;
}

export async function runOnce(options = {}) {
  const paths = options.paths || getPaths();
  const fetchSnapshot = options.fetchSnapshot || fetchUsageSnapshot;
  const now = options.now || Date.now;
  const log = options.log || console;
  if (!options.watchOwner) {
    const watchLock = await readJson(paths.watchLock);
    if (watchLock && lockProcessIsRunning(watchLock)) {
      log.log('Codex helper watch is running. Stop it before running once.');
      return 2;
    }
  }
  const lock = await acquireLock(paths.runLock, options.watchOwner ? 'watch' : 'once');
  if (!lock) {
    log.log('Codex helper is busy. Close the login browser or stop watch before trying again.');
    return 2;
  }
  try {
    try {
      const result = await fetchSnapshot(paths.profile);
      if (result?.state !== 'ok') {
        const error = new HelperError(safeState(result));
        error.details = result;
        throw error;
      }
      const snapshot = sanitizeSnapshot(result.payload, now());
      if (!snapshot) throw new HelperError('usage_unavailable');
      await atomicWriteJson(paths.usage, snapshot);
      await writeStatus(paths, 'ok', {}, now());
      log.log('Codex helper updated usage.');
      return 0;
    } catch (error) {
      // Do not retain a previous successful value after any failed request.
      await removeFile(paths.usage);
      const state = safeState(error);
      await writeStatus(paths, state, error?.details, now());
      log.error(`Codex helper failed: ${state}.`);
      return 1;
    }
  } finally {
    await lock.release();
  }
}

export async function runLogin(paths = getPaths()) {
  const watchLock = await readJson(paths.watchLock);
  if (watchLock && lockProcessIsRunning(watchLock)) {
    console.log('Codex watch is running. Stop it before opening the login browser.');
    return 2;
  }
  const lock = await acquireLock(paths.runLock, 'login');
  if (!lock) {
    console.log('Codex helper is busy. Retry after the current operation finishes.');
    return 2;
  }
  try {
    await removeFile(paths.usage);
    await openLoginBrowser(paths.profile);
    await writeStatus(paths, 'login_browser_opened');
    console.log('Opened the persistent dedicated Chrome profile for Codex login. Existing sign-in is reused.');
    console.log('Sign in there, then close that browser window before running once/start.');
    return 0;
  } catch (error) {
    await writeStatus(paths, safeState(error));
    console.error(`Codex helper failed: ${safeState(error)}.`);
    return 1;
  } finally {
    await lock.release();
  }
}

export async function runReset(paths = getPaths(), options = {}) {
  const log = options.log || console;
  const watchLock = await readJson(paths.watchLock);
  if (watchLock && lockProcessIsRunning(watchLock)) {
    log.log('Codex watch is running. Stop it before resetting the profile.');
    return 2;
  }
  // Match watch's lock order so start cannot appear between the watch check
  // and recursive profile removal. Both OS locks are held until reset ends.
  const watchGuard = await acquireLock(paths.watchLock, 'reset');
  if (!watchGuard) {
    log.log('Codex helper is busy. Retry reset after the current operation finishes.');
    return 2;
  }
  let lock;
  try {
    lock = await acquireLock(paths.runLock, 'reset');
  } catch (error) {
    await watchGuard.release();
    throw error;
  }
  if (!lock) {
    await watchGuard.release();
    log.log('Codex helper is busy. Retry reset after the current operation finishes.');
    return 2;
  }
  try {
    const base = path.resolve(paths.base);
    const profile = path.resolve(paths.profile);
    if (profile !== path.join(base, 'codex-browser-profile')) throw new HelperError('reset_refused');
    const baseInfo = await fsp.lstat(base);
    if (!baseInfo.isDirectory() || baseInfo.isSymbolicLink()) throw new HelperError('reset_refused');
    const profileInfo = await fsp.lstat(profile).catch((error) => {
      if (error.code === 'ENOENT') return null;
      throw error;
    });
    if (profileInfo && (!profileInfo.isDirectory() || profileInfo.isSymbolicLink())) throw new HelperError('reset_refused');
    if ((options.isProfileInUse || isProfileInUse)(profile)) throw new HelperError('profile_in_use');
    // Verify the parent is the directory we resolved, before recursive removal.
    const realBase = await fsp.realpath(base);
    if (profileInfo && path.dirname(await fsp.realpath(profile)) !== realBase) throw new HelperError('reset_refused');
    await fsp.rm(profile, { recursive: true, force: true });
    for (const file of [paths.usage, paths.status, paths.stop]) await removeFile(file);
    await writeStatus(paths, 'reset');
    log.log('Reset the dedicated Codex helper profile and local snapshot.');
    return 0;
  } catch (error) {
    log.error(`Codex helper reset refused: ${safeState(error)}.`);
    return 1;
  } finally {
    try { await lock.release(); } finally { await watchGuard.release(); }
  }
}

export async function runWatch(options = {}) {
  const paths = options.paths || getPaths();
  const lock = await acquireLock(paths.watchLock, 'watch');
  if (!lock) {
    console.log('Codex helper watch is already running.');
    return 0;
  }
  let stopping = false;
  let checking = false;
  const stop = () => {
    stopping = true;
    void closeActiveBrowser().catch(() => {});
  };
  const checkStop = async () => {
    if (checking || stopping) return;
    checking = true;
    try {
      const request = await readJson(paths.stop);
      if (request?.pid === lock.value.pid && request.started_at === lock.value.started_at) stop();
    } finally { checking = false; }
  };
  const timer = setInterval(() => { void checkStop().catch(() => {}); }, 250);
  process.once('SIGINT', stop);
  process.once('SIGTERM', stop);
  try {
    // once should not compete for the browser profile while watch is waiting.
    while (!stopping) {
      const started = Date.now();
      await runOnce({ ...options, paths, watchOwner: lock.value });
      const due = started + REFRESH_MS;
      while (!stopping && Date.now() < due) {
        await new Promise((resolve) => setTimeout(resolve, Math.min(250, due - Date.now())));
      }
    }
  } finally {
    clearInterval(timer);
    process.removeListener('SIGINT', stop);
    process.removeListener('SIGTERM', stop);
    await closeActiveBrowser();
    await removeFile(paths.usage);
    await writeStatus(paths, 'stopped');
    await removeFile(paths.stop);
    await lock.release();
  }
  return 0;
}

export async function showStatus(paths = getPaths()) {
  const status = await readJson(paths.status);
  const usage = await readJson(paths.usage);
  const updated = Date.parse(usage?.updated_at);
  const now = Date.now();
  const fresh = usage?.source === 'codex-web' && usage.schema_version === 1
    && Number.isFinite(updated) && updated <= now + 30_000 && now - updated <= 180_000
    && Boolean(sanitizeSnapshot(usage, now));
  console.log(`Status: ${STATES.has(status?.state) ? status.state : 'unknown'}`);
  console.log(`Usage: ${fresh ? 'fresh' : 'unavailable'}`);
  console.log(`Profile: ${isProfileInUse(paths.profile) ? 'in use' : 'closed'}`);
  return 0;
}

async function main() {
  const mode = (process.argv[2] || 'once').toLowerCase();
  if (mode === 'once') return runOnce();
  if (mode === 'login') return runLogin();
  if (mode === 'watch') return runWatch();
  if (mode === 'status') return showStatus();
  if (mode === 'reset') return runReset();
  console.error('Use one of: login, once, watch, status, reset. Use the PowerShell wrapper for start/stop.');
  return 1;
}

if (process.argv[1] && path.resolve(process.argv[1]) === SCRIPT_PATH) {
  main().then((code) => { process.exitCode = code; }).catch(async () => {
    try {
      const paths = getPaths();
      await fsp.mkdir(paths.base, { recursive: true });
      await removeFile(paths.usage);
      await writeStatus(paths, 'storage_failed');
    } catch { /* Never print an exception containing a response or credential. */ }
    console.error('Codex helper failed: storage_failed.');
    process.exitCode = 1;
  });
}
