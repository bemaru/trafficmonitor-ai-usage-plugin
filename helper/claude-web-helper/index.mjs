import fs from 'node:fs';
import fsp from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import process from 'node:process';
import { chromium } from 'playwright-core';

const MODE = (process.argv[2] || 'once').toLowerCase();
const DEFAULT_REFRESH_MS = parseInt(process.env.CLAUDE_WEB_HELPER_REFRESH_MS || '60000', 10);
const BASE_DIR = process.env.LOCALAPPDATA
  ? path.join(process.env.LOCALAPPDATA, 'trafficmonitor-claude-usage-plugin')
  : path.join(os.homedir(), '.cache', 'trafficmonitor-claude-usage-plugin');
const AUTH_STATE_PATH = path.join(BASE_DIR, 'claude-web-auth.json');
const USAGE_CACHE_PATH = path.join(BASE_DIR, 'claude-web-usage.json');
const STATUS_PATH = path.join(BASE_DIR, 'claude-web-helper-status.json');
const USER_AGENT =
  'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/135.0.0.0 Safari/537.36';

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

async function createBrowserContext({ headless }) {
  const executablePath = getBrowserPath();
  if (!executablePath) {
    throw new Error('Browser executable not found. Set CLAUDE_WEB_HELPER_BROWSER.');
  }

  const browser = await chromium.launch({
    executablePath,
    headless,
  });

  const contextOptions = {
    userAgent: USER_AGENT,
  };

  if (fs.existsSync(AUTH_STATE_PATH)) {
    contextOptions.storageState = AUTH_STATE_PATH;
  }

  const context = await browser.newContext(contextOptions);
  const page = await context.newPage();
  return { browser, context, page };
}

async function fetchJsonViaPage(page, url) {
  const result = await page.evaluate(async (targetUrl) => {
    const response = await fetch(targetUrl, {
      credentials: 'include',
      headers: {
        accept: 'application/json',
      },
    });
    const text = await response.text();
    return {
      ok: response.ok,
      status: response.status,
      text,
    };
  }, url);

  if (!result.ok) {
    const error = new Error(`HTTP ${result.status}`);
    error.code = 'HTTP_ERROR';
    error.httpStatus = result.status;
    error.body = result.text;
    throw error;
  }

  if (!isProbablyJson(result.text)) {
    const error = new Error(`Non-JSON response: ${classifyBody(result.text)}`);
    error.code = 'NON_JSON';
    error.body = result.text;
    throw error;
  }

  return JSON.parse(result.text);
}

async function fetchUsageSnapshot({ headless }) {
  const { browser, context, page } = await createBrowserContext({ headless });
  try {
    await page.goto('https://claude.ai', { waitUntil: 'domcontentloaded', timeout: 30000 });
    const organizations = await fetchJsonViaPage(page, 'https://claude.ai/api/organizations');
    if (!Array.isArray(organizations) || organizations.length === 0) {
      throw new Error('No Claude organizations available');
    }

    const organization = organizations[0];
    const organizationId = organization.uuid || organization.id;
    if (!organizationId) {
      throw new Error('Organization id not found');
    }

    const usage = await fetchJsonViaPage(page, `https://claude.ai/api/organizations/${organizationId}/usage`);
    const payload = normalizeUsagePayload(usage);
    await context.storageState({ path: AUTH_STATE_PATH });
    return { organizationId, payload };
  } finally {
    await context.close();
    await browser.close();
  }
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
  if (message.includes('cloudflare_blocked') || message.includes('cloudflare_challenge')) {
    return 'cloudflare_blocked';
  }
  if (message.includes('Non-JSON response')) {
    return 'login_required';
  }
  return 'request_failed';
}

async function writeUsagePayload(payload, organizationId) {
  await ensureBaseDir();
  await atomicWriteJson(USAGE_CACHE_PATH, payload);
  await writeStatus('ok', {
    organization_id: organizationId,
    usage_path: USAGE_CACHE_PATH,
  });
}

async function runOnce({ headless }) {
  try {
    const { organizationId, payload } = await fetchUsageSnapshot({ headless });
    await writeUsagePayload(payload, organizationId);
    console.log(`Claude helper updated ${USAGE_CACHE_PATH}`);
    return 0;
  } catch (error) {
    await ensureBaseDir();
    await removeFileIfExists(USAGE_CACHE_PATH);
    await writeStatus(classifyError(error), {
      error: error && error.message ? error.message : String(error),
    });
    console.error(`Claude helper failed: ${error && error.message ? error.message : error}`);
    return 1;
  }
}

async function runLogin() {
  await ensureBaseDir();
  const { browser, context, page } = await createBrowserContext({ headless: false });
  try {
    await page.goto('https://claude.ai/login', { waitUntil: 'domcontentloaded', timeout: 30000 });
    console.log('Complete the Claude login in the opened browser window.');
    console.log('The helper will save auth state and close automatically after a successful usage fetch.');

    const deadline = Date.now() + 10 * 60 * 1000;
    while (Date.now() < deadline) {
      if (page.isClosed()) {
        throw new Error('Login window closed before authentication completed');
      }

      try {
        const organizations = await fetchJsonViaPage(page, 'https://claude.ai/api/organizations');
        if (Array.isArray(organizations) && organizations.length > 0) {
          const organization = organizations[0];
          const organizationId = organization.uuid || organization.id;
          const usage = await fetchJsonViaPage(page, `https://claude.ai/api/organizations/${organizationId}/usage`);
          const payload = normalizeUsagePayload(usage);
          await context.storageState({ path: AUTH_STATE_PATH });
          await writeUsagePayload(payload, organizationId);
          console.log(`Claude login complete. Auth saved to ${AUTH_STATE_PATH}`);
          return 0;
        }
      } catch (error) {
        const state = classifyError(error);
        await writeStatus(state, {
          error: error && error.message ? error.message : String(error),
        });
      }

      await delay(2000);
    }

    throw new Error('Login timed out after 10 minutes');
  } finally {
    await context.close();
    await browser.close();
  }
}

async function runWatch() {
  while (true) {
    await runOnce({ headless: true });
    await delay(DEFAULT_REFRESH_MS);
  }
}

async function main() {
  switch (MODE) {
    case 'login':
      process.exitCode = await runLogin();
      break;
    case 'once':
      process.exitCode = await runOnce({ headless: true });
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
