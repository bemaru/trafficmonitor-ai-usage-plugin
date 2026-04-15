import crypto from 'node:crypto';
import fs from 'node:fs';
import fsp from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import process from 'node:process';
import { spawn, spawnSync } from 'node:child_process';
import { DatabaseSync } from 'node:sqlite';

const MODE = (process.argv[2] || 'once').toLowerCase();
const DEFAULT_REFRESH_MS = parseInt(process.env.CLAUDE_WEB_HELPER_REFRESH_MS || '60000', 10);
const BASE_DIR = process.env.LOCALAPPDATA
  ? path.join(process.env.LOCALAPPDATA, 'trafficmonitor-claude-usage-plugin')
  : path.join(os.homedir(), '.cache', 'trafficmonitor-claude-usage-plugin');
const PROFILE_DIR = path.join(BASE_DIR, 'claude-browser-profile');
const LOCAL_STATE_PATH = path.join(PROFILE_DIR, 'Local State');
const COOKIES_DB_PATH = path.join(PROFILE_DIR, 'Default', 'Network', 'Cookies');
const USAGE_CACHE_PATH = path.join(BASE_DIR, 'claude-web-usage.json');
const STATUS_PATH = path.join(BASE_DIR, 'claude-web-helper-status.json');
const USER_AGENT =
  'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/135.0.0.0 Safari/537.36';

let cachedMasterKey = null;

function delay(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function getBrowserPath() {
  const override = process.env.CLAUDE_WEB_HELPER_BROWSER;
  if (override && fs.existsSync(override)) {
    return override;
  }

  const candidates = [
    'C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe',
    'C:\\Program Files\\Microsoft\\Edge\\Application\\msedge.exe',
    'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe',
    'C:\\Program Files (x86)\\Google\\Chrome\\Application\\chrome.exe',
  ];

  return candidates.find((candidate) => fs.existsSync(candidate)) || null;
}

async function ensureBaseDir() {
  await fsp.mkdir(BASE_DIR, { recursive: true });
}

async function ensureProfileDir() {
  await fsp.mkdir(PROFILE_DIR, { recursive: true });
}

async function atomicWriteJson(filePath, value) {
  const tempPath = `${filePath}.tmp`;
  const text = `${JSON.stringify(value, null, 2)}\n`;
  await fsp.writeFile(tempPath, text, 'utf8');
  await fsp.rename(tempPath, filePath);
}

async function removeFileIfExists(filePath) {
  try {
    await fsp.unlink(filePath);
  } catch (error) {
    if (error && error.code !== 'ENOENT') {
      throw error;
    }
  }
}

async function writeStatus(state, details = {}) {
  await ensureBaseDir();
  await atomicWriteJson(STATUS_PATH, {
    state,
    updated_at: new Date().toISOString(),
    ...details,
  });
}

function isProbablyJson(text) {
  const trimmed = String(text || '').trim();
  return trimmed.startsWith('{') || trimmed.startsWith('[');
}

function classifyBody(text) {
  const body = String(text || '');
  if (!body) {
    return 'empty';
  }
  if (body.includes('Just a moment')) {
    return 'cloudflare_blocked';
  }
  if (body.includes('Enable JavaScript and cookies to continue')) {
    return 'cloudflare_challenge';
  }
  if (body.includes('<html')) {
    return 'unexpected_html';
  }
  return 'unknown';
}

function normalizeUsagePayload(raw) {
  if (!raw || typeof raw !== 'object') {
    throw new Error('Invalid usage payload');
  }

  if (!raw.five_hour && !raw.seven_day) {
    throw new Error('Usage payload missing five_hour/seven_day');
  }

  return {
    source: 'claude-web-helper',
    generated_at: new Date().toISOString(),
    five_hour: raw.five_hour || null,
    seven_day: raw.seven_day || null,
    seven_day_sonnet: raw.seven_day_sonnet || null,
    extra_usage: raw.extra_usage || null,
  };
}

function getLocalStatePayload() {
  if (!fs.existsSync(LOCAL_STATE_PATH)) {
    throw new Error(`Claude helper local state not found at ${LOCAL_STATE_PATH}`);
  }

  return JSON.parse(fs.readFileSync(LOCAL_STATE_PATH, 'utf8'));
}

function runPowerShell(script) {
  const encodedCommand = Buffer.from(script, 'utf16le').toString('base64');
  const candidates = ['pwsh', 'powershell.exe'];

  for (const executable of candidates) {
    const result = spawnSync(executable, ['-NoProfile', '-EncodedCommand', encodedCommand], {
      encoding: 'utf8',
      windowsHide: true,
    });
    if (!result.error && result.status === 0) {
      return result.stdout.trim();
    }
  }

  throw new Error('Failed to run PowerShell for Chromium DPAPI decryption');
}

function getMasterKey() {
  if (cachedMasterKey) {
    return cachedMasterKey;
  }

  const localState = getLocalStatePayload();
  const encryptedKeyBase64 = localState?.os_crypt?.encrypted_key;
  if (!encryptedKeyBase64) {
    throw new Error('Chromium master key missing from Local State');
  }

  const encryptedKey = Buffer.from(encryptedKeyBase64, 'base64');
  const prefix = encryptedKey.subarray(0, 5).toString('utf8');
  if (prefix !== 'DPAPI') {
    throw new Error(`Unsupported Chromium key prefix: ${prefix || 'unknown'}`);
  }

  const dpapiPayload = encryptedKey.subarray(5);
  const pwshScript =
    `[Convert]::ToBase64String(` +
    `[System.Security.Cryptography.ProtectedData]::Unprotect(` +
    `[Convert]::FromBase64String('${dpapiPayload.toString('base64')}'), ` +
    `$null, ` +
    `[System.Security.Cryptography.DataProtectionScope]::CurrentUser))`;
  cachedMasterKey = Buffer.from(runPowerShell(pwshScript), 'base64');
  if (!cachedMasterKey.length) {
    throw new Error('Chromium master key was empty');
  }

  return cachedMasterKey;
}

function copyCookiesDbForRead() {
  if (!fs.existsSync(COOKIES_DB_PATH)) {
    throw new Error(`Claude helper cookies DB not found at ${COOKIES_DB_PATH}`);
  }

  const tempPath = path.join(os.tmpdir(), `tm-claude-cookies-${process.pid}-${Date.now()}.sqlite`);
  try {
    fs.copyFileSync(COOKIES_DB_PATH, tempPath);
  } catch (error) {
    if (error && (error.code === 'EPERM' || error.code === 'EBUSY')) {
      throw new Error('Claude helper browser profile is still in use. Close the helper browser window and retry.');
    }
    throw error;
  }

  return tempPath;
}

function decryptCookieValue(encryptedValue, hostKey, masterKey) {
  if (!encryptedValue || !encryptedValue.length) {
    return '';
  }

  const payload = Buffer.from(encryptedValue);
  const version = payload.subarray(0, 3).toString('utf8');
  if (version !== 'v10' && version !== 'v11') {
    throw new Error(`Unsupported Chromium cookie version: ${version || 'unknown'}`);
  }

  const nonce = payload.subarray(3, 15);
  const ciphertext = payload.subarray(15, payload.length - 16);
  const authTag = payload.subarray(payload.length - 16);
  const decipher = crypto.createDecipheriv('aes-256-gcm', masterKey, nonce);
  decipher.setAuthTag(authTag);

  let plaintext = Buffer.concat([decipher.update(ciphertext), decipher.final()]);
  const hostHash = crypto.createHash('sha256').update(hostKey).digest();
  if (plaintext.length >= 32 && plaintext.subarray(0, 32).equals(hostHash)) {
    plaintext = plaintext.subarray(32);
  }

  return plaintext.toString('utf8');
}

function loadClaudeCookies() {
  const masterKey = getMasterKey();
  const tempDbPath = copyCookiesDbForRead();

  try {
    const database = new DatabaseSync(tempDbPath, { readonly: true });
    try {
      const rows = database
        .prepare(
          `select host_key, name, value, encrypted_value
           from cookies
           where host_key like '%claude.ai%'
           order by case when host_key = '.claude.ai' then 0 else 1 end, name`,
        )
        .all();

      const cookieMap = new Map();
      for (const row of rows) {
        if (cookieMap.has(row.name)) {
          continue;
        }

        let value = row.value || '';
        if (!value) {
          value = decryptCookieValue(row.encrypted_value, row.host_key, masterKey);
        }

        if (value) {
          cookieMap.set(row.name, value);
        }
      }

      if (!cookieMap.has('sessionKey')) {
        throw new Error('Claude helper session cookie not found. Run login again.');
      }

      return cookieMap;
    } finally {
      database.close();
    }
  } finally {
    try {
      fs.unlinkSync(tempDbPath);
    } catch {
      // ignore temp cleanup failure
    }
  }
}

function buildCookieHeader(cookieMap) {
  return Array.from(cookieMap.entries())
    .map(([name, value]) => `${name}=${value}`)
    .join('; ');
}

async function fetchJsonWithCookies(url, cookieHeader) {
  const response = await fetch(url, {
    headers: {
      accept: 'application/json',
      cookie: cookieHeader,
      origin: 'https://claude.ai',
      referer: 'https://claude.ai/',
      'user-agent': USER_AGENT,
    },
  });

  const text = await response.text();
  if (!response.ok) {
    const error = new Error(`HTTP ${response.status}`);
    error.code = 'HTTP_ERROR';
    error.httpStatus = response.status;
    error.body = text;
    throw error;
  }

  if (!isProbablyJson(text)) {
    const error = new Error(`Non-JSON response: ${classifyBody(text)}`);
    error.code = 'NON_JSON';
    error.body = text;
    throw error;
  }

  return JSON.parse(text);
}

function pickClaudeOrganization(organizations, lastActiveOrg) {
  if (lastActiveOrg) {
    const activeOrganization = organizations.find(
      (organization) => organization?.uuid === lastActiveOrg || String(organization?.id) === String(lastActiveOrg),
    );
    if (activeOrganization) {
      return activeOrganization;
    }
  }

  return (
    organizations.find((organization) => Array.isArray(organization?.capabilities) && organization.capabilities.includes('chat')) ||
    organizations.find((organization) => typeof organization?.rate_limit_tier === 'string' && organization.rate_limit_tier.includes('claude')) ||
    organizations[0]
  );
}

function shouldRetryWithOrganizationLookup(error) {
  return Boolean(error && error.httpStatus && (error.httpStatus === 400 || error.httpStatus === 404));
}

async function fetchUsageForOrganization(cookieHeader, organizationId, organizationName = null) {
  if (!organizationId) {
    throw new Error('Organization id not found');
  }

  const usage = await fetchJsonWithCookies(`https://claude.ai/api/organizations/${organizationId}/usage`, cookieHeader);
  const payload = normalizeUsagePayload(usage);
  return { organizationId, organizationName, payload };
}

async function fetchUsageSnapshot() {
  const cookies = loadClaudeCookies();
  const cookieHeader = buildCookieHeader(cookies);
  const lastActiveOrg = cookies.get('lastActiveOrg');
  if (lastActiveOrg) {
    try {
      return await fetchUsageForOrganization(cookieHeader, lastActiveOrg);
    } catch (error) {
      if (!shouldRetryWithOrganizationLookup(error)) {
        throw error;
      }
    }
  }

  const organizations = await fetchJsonWithCookies('https://claude.ai/api/organizations', cookieHeader);
  if (!Array.isArray(organizations) || organizations.length === 0) {
    throw new Error('No Claude organizations available');
  }

  const organization = pickClaudeOrganization(organizations, lastActiveOrg);
  const organizationId = organization?.uuid || organization?.id;
  return fetchUsageForOrganization(cookieHeader, organizationId, organization?.name || null);
}

function classifyError(error) {
  const message = error && error.message ? error.message : String(error);
  if (error && error.httpStatus === 401) {
    return 'login_required';
  }
  if (error && error.httpStatus === 403) {
    return 'access_denied';
  }
  if (error && error.httpStatus === 429) {
    return 'rate_limited';
  }
  if (message.includes('browser profile is still in use')) {
    return 'profile_in_use';
  }
  if (message.includes('session cookie not found')) {
    return 'login_required';
  }
  if (message.includes('cloudflare_blocked') || message.includes('cloudflare_challenge')) {
    return 'cloudflare_blocked';
  }
  if (message.includes('Non-JSON response')) {
    return 'login_required';
  }
  return 'request_failed';
}

function shouldRetainUsageSnapshotOnFailure(state) {
  return state === 'request_failed';
}

async function writeUsagePayload(payload, organizationId, organizationName) {
  await ensureBaseDir();
  await atomicWriteJson(USAGE_CACHE_PATH, payload);
  await writeStatus('ok', {
    organization_id: organizationId,
    organization_name: organizationName,
    usage_path: USAGE_CACHE_PATH,
  });
}

async function runOnce() {
  try {
    const { organizationId, organizationName, payload } = await fetchUsageSnapshot();
    await writeUsagePayload(payload, organizationId, organizationName);
    console.log(`Claude helper updated ${USAGE_CACHE_PATH}`);
    return 0;
  } catch (error) {
    await ensureBaseDir();
    const state = classifyError(error);
    const retainedUsageSnapshot = shouldRetainUsageSnapshotOnFailure(state) && fs.existsSync(USAGE_CACHE_PATH);
    if (!retainedUsageSnapshot) {
      await removeFileIfExists(USAGE_CACHE_PATH);
    }
    await writeStatus(state, {
      error: error && error.message ? error.message : String(error),
      retained_usage_snapshot: retainedUsageSnapshot,
      usage_path: retainedUsageSnapshot ? USAGE_CACHE_PATH : undefined,
    });
    console.error(
      `Claude helper failed: ${error && error.message ? error.message : error}${
        retainedUsageSnapshot ? ' (keeping recent usage snapshot)' : ''
      }`,
    );
    return 1;
  }
}

async function runLogin() {
  await ensureBaseDir();
  await ensureProfileDir();

  const executablePath = getBrowserPath();
  if (!executablePath) {
    throw new Error('Browser executable not found. Set CLAUDE_WEB_HELPER_BROWSER.');
  }

  spawn(executablePath, [`--user-data-dir=${PROFILE_DIR}`, '--new-window', 'https://claude.ai/login'], {
    detached: true,
    stdio: 'ignore',
  }).unref();

  await writeStatus('login_browser_opened', {
    profile_dir: PROFILE_DIR,
  });

  console.log('Opened a normal browser window for Claude login.');
  console.log('Complete the login there, then close that helper browser window before running once/watch.');
  return 0;
}

async function runWatch() {
  while (true) {
    await runOnce();
    await delay(DEFAULT_REFRESH_MS);
  }
}

async function main() {
  switch (MODE) {
    case 'login':
      process.exitCode = await runLogin();
      break;
    case 'once':
      process.exitCode = await runOnce();
      break;
    case 'watch':
      await runWatch();
      break;
    default:
      console.error(`Unknown mode: ${MODE}`);
      console.error('Use one of: login, once, watch');
      process.exitCode = 1;
      break;
  }
}

main().catch(async (error) => {
  try {
    await ensureBaseDir();
    await writeStatus('crashed', {
      error: error && error.message ? error.message : String(error),
    });
  } catch {
    // ignore secondary failure
  }
  console.error(error);
  process.exitCode = 1;
});
