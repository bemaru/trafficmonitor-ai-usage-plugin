import assert from 'node:assert/strict';
import test from 'node:test';
import vm from 'node:vm';
import { normalizeUsagePayload, usageExpression } from './usage.mjs';

const NOW = 1_800_000_000_000;
function rawUsage(now = NOW) {
  return {
    rate_limit: {
      primary_window: { used_percent: 21.5, limit_window_seconds: 18000, reset_at: Math.floor(now / 1000) + 3600 },
      secondary_window: { used_percent: 79, limit_window_seconds: 604800, reset_at: Math.floor(now / 1000) + 86400 },
    },
    accessToken: 'must-not-escape',
    user_id: 'private-user',
  };
}

test('parses percentages/reset timestamps and allowlists usage fields', () => {
  assert.deepEqual(normalizeUsagePayload(rawUsage(), NOW), {
    five_hour: { utilization: 21.5, resets_at: new Date(NOW + 3_600_000).toISOString() },
    seven_day: { utilization: 79, resets_at: new Date(NOW + 86_400_000).toISOString() },
  });
});

test('window durations identify 5h/weekly even when primary and secondary are swapped', () => {
  const usage = rawUsage();
  [usage.rate_limit.primary_window, usage.rate_limit.secondary_window]
    = [usage.rate_limit.secondary_window, usage.rate_limit.primary_window];
  assert.equal(normalizeUsagePayload(usage, NOW).five_hour.utilization, 21.5);
});

test('zero usage is valid, missing/null/string/out-of-range percentages are unavailable', () => {
  for (const value of [undefined, null, '20', -1, 101, NaN, Infinity]) {
    const usage = rawUsage();
    usage.rate_limit.primary_window.used_percent = value;
    assert.equal(normalizeUsagePayload(usage, NOW), null);
  }
  const usage = rawUsage();
  usage.rate_limit.primary_window.used_percent = 0;
  assert.equal(normalizeUsagePayload(usage, NOW).five_hour.utilization, 0);
});

test('rejects missing, duplicate, expired and implausible reset windows', () => {
  for (const mutate of [
    (raw) => { raw.rate_limit.secondary_window = null; },
    (raw) => { raw.rate_limit.secondary_window.limit_window_seconds = 18000; },
    (raw) => { raw.rate_limit.primary_window.reset_at = NOW / 1000; },
    (raw) => { raw.rate_limit.primary_window.reset_at = NOW / 1000 + 604800; },
    (raw) => { raw.rate_limit.primary_window.reset_at = '1800003600'; },
  ]) {
    const raw = rawUsage();
    mutate(raw);
    assert.equal(normalizeUsagePayload(raw, NOW), null);
  }
});

function jsonResponse(body, status = 200, contentType = 'application/json', extraHeaders = {}) {
  return {
    ok: status >= 200 && status < 300,
    status,
    headers: new Headers({ 'content-type': contentType, ...extraHeaders }),
    json: async () => body,
  };
}

async function inBrowser(responses, options = {}) {
  const requests = [];
  const context = {
    location: { origin: options.origin || 'https://chatgpt.com' },
    document: { title: options.title || 'ChatGPT', querySelector: () => null },
    setTimeout, clearTimeout, AbortController,
    fetch: async (url, init) => {
      requests.push({ url, init });
      const response = responses.shift();
      if (response instanceof Error) throw response;
      return response;
    },
  };
  const result = await vm.runInNewContext(usageExpression(), context);
  return { result: JSON.parse(JSON.stringify(result)), requests };
}

test('uses authenticated session then Bearer usage in browser; secrets never return', async () => {
  const token = 'session-token-DO-NOT-RETURN';
  const now = Date.now();
  const { result, requests } = await inBrowser([
    jsonResponse({ accessToken: token, expires: new Date(now + 3_600_000).toISOString(), account: { id: 'account-id' }, user: { email: 'private@example.test' } }),
    jsonResponse({ ...rawUsage(now), account_id: 'account-id', credits: { private_data: token } }),
  ]);
  assert.equal(result.state, 'ok');
  assert.equal(requests.length, 2);
  assert.equal(requests[0].url, 'https://chatgpt.com/api/auth/session');
  assert.equal(requests[1].url, 'https://chatgpt.com/backend-api/wham/usage');
  assert.equal(requests[1].init.headers.authorization, `Bearer ${token}`);
  assert.equal(requests[1].init.headers['ChatGPT-Account-ID'], 'account-id');
  for (const request of requests) {
    assert.equal(request.init.credentials, 'include');
    assert.equal(request.init.redirect, 'error');
    assert.equal(request.init.cache, 'no-store');
  }
  assert.doesNotMatch(JSON.stringify(result), /session-token|private@example|private_data|account-id|must-not-escape/);
});

test('missing/expired session never requests usage', async () => {
  for (const session of [{}, { accessToken: '' }, { accessToken: 'secret', expires: '2000-01-01T00:00:00Z' }]) {
    const { result, requests } = await inBrowser([jsonResponse(session)]);
    assert.equal(result.state, 'login_required');
    assert.equal(requests.length, 1);
  }
});

test('HTTP errors and Cloudflare challenge produce only sanitized status', async () => {
  for (const [status, expected] of [[401, 'login_required'], [403, 'access_denied'], [429, 'rate_limited'], [500, 'http_error']]) {
    const { result } = await inBrowser([jsonResponse({ private: 'DO-NOT-RETURN' }, status)]);
    assert.deepEqual(result, { state: expected, endpoint: 'session', http_status: status });
  }
  const { result } = await inBrowser([jsonResponse('private HTML', 403, 'text/html', { 'cf-mitigated': 'challenge' })]);
  assert.equal(result.state, 'cloudflare_blocked');
  const challenged = await inBrowser([], { title: 'Just a moment...' });
  assert.equal(challenged.result.state, 'cloudflare_blocked');
  assert.equal(challenged.requests.length, 0);
});

test('HTML, invalid JSON and thrown private errors never escape', async () => {
  const badJson = jsonResponse({});
  badJson.json = async () => { throw new Error('private token'); };
  for (const response of [jsonResponse('<html>private token</html>', 200, 'text/html'), badJson, new Error('private token')]) {
    const { result } = await inBrowser([response]);
    assert.ok(['unexpected_response', 'request_failed'].includes(result.state));
    assert.doesNotMatch(JSON.stringify(result), /private|token/);
  }
});

test('does not choose an arbitrary account or publish usage for a mismatched account', async () => {
  const { result } = await inBrowser([
    jsonResponse({ accessToken: 'secret', account: { id: 'selected-account' } }),
    jsonResponse({ ...rawUsage(Date.now()), account_id: 'different-account' }),
  ]);
  assert.equal(result.state, 'account_unavailable');
});
