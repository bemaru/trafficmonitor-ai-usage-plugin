// This function is also evaluated inside Chrome. Keep it self-contained and
// return an allowlist of numeric usage data, never fields from the session.
export function normalizeUsagePayload(raw, now = Date.now()) {
  if (!raw || typeof raw !== 'object' || !raw.rate_limit || typeof raw.rate_limit !== 'object') {
    return null;
  }

  const windows = [raw.rate_limit.primary_window, raw.rate_limit.secondary_window];
  const result = {};
  for (const value of windows) {
    if (!value || typeof value !== 'object') {
      continue;
    }
    const name = value.limit_window_seconds === 18000
      ? 'five_hour'
      : value.limit_window_seconds === 604800 ? 'seven_day' : null;
    if (!name || Object.hasOwn(result, name)) {
      return null;
    }
    if (typeof value.used_percent !== 'number' || !Number.isFinite(value.used_percent)
      || value.used_percent < 0 || value.used_percent > 100
      || !Number.isSafeInteger(value.reset_at) || value.reset_at * 1000 <= now
      || value.reset_at * 1000 > now + value.limit_window_seconds * 1000 + 60_000) {
      return null;
    }
    result[name] = {
      utilization: value.used_percent,
      resets_at: new Date(value.reset_at * 1000).toISOString(),
    };
  }

  // A missing window is unavailable, not a zero percentage. Do not publish a
  // partially valid response and accidentally retain a metric from an old run.
  return result.five_hour && result.seven_day ? result : null;
}

// Runs in the dedicated browser context. Session cookies and accessToken stay
// in Chrome; CDP receives only this function's sanitized return value.
export async function requestUsageInBrowser(normalize) {
  const failure = (state, endpoint, httpStatus) => ({
    state,
    endpoint,
    ...(Number.isInteger(httpStatus) && httpStatus > 0 ? { http_status: httpStatus } : {}),
  });
  const classifyHttp = (response, endpoint) => {
    if (response.headers.get('cf-mitigated') === 'challenge') return failure('cloudflare_blocked', endpoint, response.status);
    if (response.status === 401) return failure('login_required', endpoint, response.status);
    if (response.status === 403) return failure('access_denied', endpoint, response.status);
    if (response.status === 429) return failure('rate_limited', endpoint, response.status);
    return failure('http_error', endpoint, response.status);
  };
  const readJson = async (response, endpoint) => {
    if (!response.ok) {
      return { error: classifyHttp(response, endpoint) };
    }
    const contentType = response.headers.get('content-type') || '';
    if (!contentType.toLowerCase().includes('application/json')) {
      return { error: failure('unexpected_response', endpoint) };
    }
    try {
      return { value: await response.json() };
    } catch {
      return { error: failure('unexpected_response', endpoint) };
    }
  };

  let endpoint = 'session';
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 20_000);
  try {
    if (location.origin !== 'https://chatgpt.com') {
      return failure('login_required', endpoint);
    }
    if (document.querySelector('#challenge-running, #challenge-form')
      || /just a moment|verify you are human/i.test(document.title)) {
      return failure('cloudflare_blocked', endpoint);
    }
    const sessionResponse = await fetch('https://chatgpt.com/api/auth/session', {
      credentials: 'include',
      cache: 'no-store',
      redirect: 'error',
      headers: { accept: 'application/json' },
      signal: controller.signal,
    });
    const sessionResult = await readJson(sessionResponse, endpoint);
    if (sessionResult.error) return sessionResult.error;
    const session = sessionResult.value;
    if (!session || typeof session.accessToken !== 'string' || !session.accessToken.trim()) {
      return failure('login_required', endpoint);
    }
    if (session.expires && (!Number.isFinite(Date.parse(session.expires)) || Date.parse(session.expires) <= Date.now())) {
      return failure('login_required', endpoint);
    }

    endpoint = 'usage';
    const headers = { accept: 'application/json', authorization: `Bearer ${session.accessToken}` };
    // Use only the account selected by the authenticated session. Never choose
    // an arbitrary workspace from an account list or persist the account ID.
    const accountId = session.account?.id;
    if (accountId !== undefined && (typeof accountId !== 'string' || !/^[A-Za-z0-9_-]{1,128}$/.test(accountId))) {
      return failure('account_unavailable', endpoint);
    }
    if (accountId) headers['ChatGPT-Account-ID'] = accountId;
    const response = await fetch('https://chatgpt.com/backend-api/wham/usage', {
      credentials: 'include',
      cache: 'no-store',
      redirect: 'error',
      headers,
      signal: controller.signal,
    });
    const usageResult = await readJson(response, endpoint);
    if (usageResult.error) return usageResult.error;
    if (accountId && usageResult.value?.account_id && usageResult.value.account_id !== accountId) {
      return failure('account_unavailable', endpoint);
    }
    const payload = normalize(usageResult.value);
    if (!payload) return failure('usage_unavailable', endpoint);
    return { state: 'ok', payload };
  } catch {
    return failure(controller.signal.aborted ? 'request_timeout' : 'request_failed', endpoint);
  } finally {
    clearTimeout(timeout);
  }
}

export function usageExpression() {
  return `(${requestUsageInBrowser.toString()})(${normalizeUsagePayload.toString()})`;
}
