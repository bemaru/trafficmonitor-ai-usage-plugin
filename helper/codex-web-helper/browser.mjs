import fs from 'node:fs';
import fsp from 'node:fs/promises';
import { spawn, spawnSync } from 'node:child_process';
import { usageExpression } from './usage.mjs';

let activeBrowser = null;

export async function closeActiveBrowser() {
  if (activeBrowser) await activeBrowser.close();
}

export class HelperError extends Error {
  constructor(state) {
    super(state);
    this.state = state;
  }
}

export function getBrowserPath() {
  const override = process.env.CODEX_WEB_HELPER_BROWSER;
  if (override && fs.existsSync(override)) return override;
  const candidates = [
    'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe',
    'C:\\Program Files (x86)\\Google\\Chrome\\Application\\chrome.exe',
    process.env.LOCALAPPDATA ? `${process.env.LOCALAPPDATA}\\Google\\Chrome\\Application\\chrome.exe` : '',
  ];
  return candidates.find((candidate) => candidate && fs.existsSync(candidate)) || null;
}

export function isProfileInUse(profileDir) {
  if (process.platform !== 'win32') return false;
  // Only inspect process identity/arguments, never browser cookie stores. Pass
  // the profile path through an environment variable, not shell interpolation.
  const script = [
    '$profilePath = $env:TM_CODEX_PROFILE_CHECK',
    `$pattern = '(?i)(^|\\s)"?--user-data-dir="?' + [regex]::Escape($profilePath) + '"?(\\s|$)'`,
    `$found = Get-CimInstance Win32_Process -Filter "Name = 'chrome.exe'" -ErrorAction Stop |`,
    'Where-Object { $_.CommandLine -and $_.CommandLine -match $pattern } | Select-Object -First 1',
    'if ($found) { exit 2 }; exit 0',
  ].join('\n');
  const result = spawnSync('powershell.exe', ['-NoProfile', '-EncodedCommand', Buffer.from(script, 'utf16le').toString('base64')], {
    windowsHide: true,
    stdio: 'ignore',
    env: { ...process.env, TM_CODEX_PROFILE_CHECK: profileDir },
    timeout: 10_000,
  });
  if (result.error || ![0, 2].includes(result.status)) throw new HelperError('process_check_failed');
  return result.status === 2;
}

export async function openLoginBrowser(profileDir) {
  const executable = getBrowserPath();
  if (!executable) throw new HelperError('browser_not_found');
  await fsp.mkdir(profileDir, { recursive: true });
  const child = spawn(executable, [
    `--user-data-dir=${profileDir}`, '--no-first-run', '--no-default-browser-check',
    '--new-window', 'https://chatgpt.com/codex/settings/usage',
  ], { detached: true, stdio: 'ignore', windowsHide: false });
  await new Promise((resolve, reject) => {
    child.once('spawn', resolve);
    child.once('error', () => reject(new HelperError('browser_start_failed')));
  });
  child.unref();
}

// Chrome's pipe transport has no listening TCP port. Neither cookies nor the
// session response are requested over CDP, and browser stdout/stderr is ignored.
export class ChromePipe {
  constructor(child) {
    this.child = child;
    this.nextId = 0;
    this.pending = new Map();
    this.buffer = '';
    this.closed = false;
    this.exitPromise = new Promise((resolve) => { this.resolveExit = resolve; });
    child.stdio[4].setEncoding('utf8');
    child.stdio[4].on('data', (chunk) => this.onData(chunk));
    child.stdio[4].on('error', () => this.fail());
    child.stdio[3].on('error', () => this.fail());
    child.once('error', () => this.fail());
    child.once('exit', () => { this.fail(); this.resolveExit(); });
  }

  fail() {
    this.closed = true;
    for (const request of this.pending.values()) {
      clearTimeout(request.timer);
      request.reject(new HelperError('browser_closed'));
    }
    this.pending.clear();
  }

  onData(chunk) {
    this.buffer += chunk;
    // CDP exceptions can contain private page state. Never forward them.
    if (this.buffer.length > 2_000_000) {
      this.fail();
      this.child.kill();
      return;
    }
    let end;
    while ((end = this.buffer.indexOf('\0')) >= 0) {
      const message = this.buffer.slice(0, end);
      this.buffer = this.buffer.slice(end + 1);
      let parsed;
      try { parsed = JSON.parse(message); } catch { continue; }
      const request = this.pending.get(parsed.id);
      if (!request) continue;
      this.pending.delete(parsed.id);
      clearTimeout(request.timer);
      if (parsed.error) request.reject(new HelperError('browser_protocol_failed'));
      else request.resolve(parsed.result);
    }
  }

  send(method, params = {}, sessionId, timeoutMs = 30_000) {
    if (this.closed) return Promise.reject(new HelperError('browser_closed'));
    const id = ++this.nextId;
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id);
        reject(new HelperError('browser_timeout'));
      }, timeoutMs);
      this.pending.set(id, { resolve, reject, timer });
      this.child.stdio[3].write(`${JSON.stringify({ id, method, params, ...(sessionId ? { sessionId } : {}) })}\0`);
    });
  }

  close() {
    if (!this.closePromise) this.closePromise = this.closeGracefully();
    return this.closePromise;
  }

  async closeGracefully() {
    if (!this.closed) {
      try { await this.send('Browser.close', {}, undefined, 2000); } catch { /* already closed */ }
    }
    if (this.child.exitCode === null) {
      // Browser.close may reply before profile cookies finish flushing. Give
      // Chrome time to exit normally before terminating an unresponsive child.
      let timer;
      await Promise.race([
        this.exitPromise,
        new Promise((resolve) => { timer = setTimeout(resolve, 2000); }),
      ]);
      clearTimeout(timer);
      if (this.child.exitCode === null) this.child.kill();
    }
    this.fail();
    this.buffer = '';
  }
}

export async function fetchUsageSnapshot(profileDir) {
  const executable = getBrowserPath();
  if (!executable) throw new HelperError('browser_not_found');
  if (isProfileInUse(profileDir)) throw new HelperError('profile_in_use');
  await fsp.mkdir(profileDir, { recursive: true });
  const child = spawn(executable, [
    `--user-data-dir=${profileDir}`, '--headless=new', '--remote-debugging-pipe',
    '--no-first-run', '--no-default-browser-check', '--disable-background-mode', 'about:blank',
  ], { windowsHide: true, stdio: ['ignore', 'ignore', 'ignore', 'pipe', 'pipe'] });
  const browser = new ChromePipe(child);
  activeBrowser = browser;
  try {
    await browser.send('Browser.getVersion', {}, undefined, 15_000);
    const { targetId } = await browser.send('Target.createTarget', { url: 'about:blank' });
    const { sessionId } = await browser.send('Target.attachToTarget', { targetId, flatten: true });
    await browser.send('Page.enable', {}, sessionId);
    const navigation = await browser.send('Page.navigate', { url: 'https://chatgpt.com/' }, sessionId);
    if (navigation.errorText) throw new HelperError('navigation_failed');
    const deadline = Date.now() + 20_000;
    let ready = false;
    while (Date.now() < deadline) {
      const state = await browser.send('Runtime.evaluate', {
        expression: "({ready:document.readyState !== 'loading', origin:location.origin === 'https://chatgpt.com'})",
        returnByValue: true,
      }, sessionId);
      if (state.result?.value?.ready && state.result?.value?.origin) {
        ready = true;
        break;
      }
      await new Promise((resolve) => setTimeout(resolve, 200));
    }
    if (!ready) throw new HelperError('navigation_failed');
    const result = await browser.send('Runtime.evaluate', {
      expression: usageExpression(), awaitPromise: true, returnByValue: true,
    }, sessionId);
    if (result.exceptionDetails || !result.result?.value) throw new HelperError('browser_evaluation_failed');
    return result.result.value;
  } finally {
    await browser.close();
    if (activeBrowser === browser) activeBrowser = null;
  }
}
