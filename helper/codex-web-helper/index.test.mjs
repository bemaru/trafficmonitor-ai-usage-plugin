import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { acquireLock, getPaths, REFRESH_MS, runOnce, runReset, runWatch } from './index.mjs';

const NOW = 1_800_000_000_000;
function payload(now = NOW) {
  return {
    five_hour: { utilization: 25, resets_at: new Date(now + 3600_000).toISOString(), accessToken: 'PRIVATE' },
    seven_day: { utilization: 75, resets_at: new Date(now + 86400_000).toISOString() },
    accessToken: 'PRIVATE',
  };
}

async function fixture(t) {
  const base = await fs.mkdtemp(path.join(os.tmpdir(), 'tm-codex-test-'));
  const paths = getPaths(base);
  t.after(() => fs.rm(base, { recursive: true, force: true }));
  const output = [];
  return { paths, output, log: { log: (line) => output.push(line), error: (line) => output.push(line) } };
}

test('success writes only allowed usage data and releases operation lock', async (t) => {
  const setup = await fixture(t);
  assert.equal(await runOnce({ ...setup, now: () => NOW, fetchSnapshot: async () => ({ state: 'ok', payload: payload() }) }), 0);
  const value = JSON.parse(await fs.readFile(setup.paths.usage, 'utf8'));
  assert.deepEqual(value, {
    schema_version: 1, source: 'codex-web', updated_at: new Date(NOW).toISOString(),
    five_hour: { utilization: 25, resets_at: new Date(NOW + 3600_000).toISOString() },
    seven_day: { utilization: 75, resets_at: new Date(NOW + 86400_000).toISOString() },
  });
  assert.doesNotMatch(await fs.readFile(setup.paths.status, 'utf8'), /PRIVATE|accessToken/);
  await assert.rejects(fs.access(setup.paths.runLock), { code: 'ENOENT' });
});

test('all failures delete a prior snapshot and redact arbitrary exception text', async (t) => {
  const setup = await fixture(t);
  for (const fetchSnapshot of [
    async () => { throw new Error('Bearer PRIVATE-SECRET cookie=PRIVATE'); },
    async () => ({ state: 'login_required', endpoint: 'session', http_status: 401, error: 'PRIVATE-SECRET' }),
    async () => ({ state: 'http_error', endpoint: 'usage', http_status: 500, body: 'PRIVATE-SECRET' }),
    async () => ({ state: 'ok', payload: { five_hour: { utilization: 0 } } }),
  ]) {
    await fs.writeFile(setup.paths.usage, JSON.stringify(payload()));
    assert.equal(await runOnce({ ...setup, now: () => NOW, fetchSnapshot }), 1);
    await assert.rejects(fs.access(setup.paths.usage), { code: 'ENOENT' });
    assert.doesNotMatch(await fs.readFile(setup.paths.status, 'utf8'), /PRIVATE|Bearer|cookie|body/);
  }
  assert.doesNotMatch(setup.output.join('\n'), /PRIVATE|Bearer|cookie/);
});

test('a competing operation neither fetches nor removes an active snapshot', async (t) => {
  const setup = await fixture(t);
  await fs.writeFile(setup.paths.runLock, '');
  await fs.writeFile(setup.paths.usage, 'existing snapshot');
  let called = false;
  assert.equal(await runOnce({ ...setup, fetchSnapshot: async () => { called = true; } }), 2);
  assert.equal(called, false);
  assert.equal(await fs.readFile(setup.paths.usage, 'utf8'), 'existing snapshot');
});

test('60-second watch accepts PowerShell 5.1 BOM stop request and cleans up without another fetch', async (t) => {
  const setup = await fixture(t);
  let calls = 0;
  assert.equal(REFRESH_MS, 60_000);
  await runWatch({
    ...setup,
    fetchSnapshot: async () => {
      calls += 1;
      const lock = JSON.parse(await fs.readFile(setup.paths.watchLock, 'utf8'));
      await fs.writeFile(setup.paths.stop, `\uFEFF${JSON.stringify({ pid: lock.pid, started_at: lock.started_at })}`);
      return { state: 'ok', payload: payload(Date.now()) };
    },
  });
  assert.equal(calls, 1);
  assert.equal(JSON.parse(await fs.readFile(setup.paths.status, 'utf8')).state, 'stopped');
  for (const key of ['usage', 'watchLock', 'runLock', 'stop']) {
    await assert.rejects(fs.access(setup.paths[key]), { code: 'ENOENT' });
  }
});

test('OS mutex grants only one owner and releases ownership for the next run', async (t) => {
  const setup = await fixture(t);
  const lock = await acquireLock(setup.paths.runLock, 'once');
  try {
    const contenders = await Promise.all(Array.from({ length: 8 }, () => acquireLock(setup.paths.runLock, 'once')));
    assert.ok(contenders.every((value) => value === null));
    assert.equal(JSON.parse(await fs.readFile(setup.paths.runLock, 'utf8')).pid, process.pid);
  } finally {
    await lock.release();
  }
  const next = await acquireLock(setup.paths.runLock, 'once');
  assert.ok(next);
  await next.release();
});

test('process crash releases OS mutex; competing stale reclaimers cannot remove the new owner', async (t) => {
  const setup = await fixture(t);
  const childCode = `import { acquireLock } from ${JSON.stringify(new URL('./index.mjs', import.meta.url).href)};
    await acquireLock(${JSON.stringify(setup.paths.runLock)}, 'once');
    process.stdout.write('locked'); setInterval(() => {}, 1000);`;
  const child = spawn(process.execPath, ['--input-type=module', '-e', childCode], {
    windowsHide: true, stdio: ['ignore', 'pipe', 'ignore'],
  });
  t.after(() => { if (child.exitCode === null && child.signalCode === null) child.kill(); });
  await once(child.stdout, 'data');
  const exited = once(child, 'exit');
  child.kill();
  await exited;
  const contenders = await Promise.all(Array.from({ length: 8 }, () => acquireLock(setup.paths.runLock, 'once')));
  const owners = contenders.filter(Boolean);
  assert.equal(owners.length, 1);
  assert.equal(JSON.parse(await fs.readFile(setup.paths.runLock, 'utf8')).pid, process.pid);
  assert.equal(await acquireLock(setup.paths.runLock, 'once'), null);
  await owners[0].release();
});

test('reset removes only the dedicated closed profile, preserving sibling profiles', async (t) => {
  const setup = await fixture(t);
  const sibling = path.join(setup.paths.base, 'claude-browser-profile');
  await fs.mkdir(setup.paths.profile);
  await fs.mkdir(sibling);
  await fs.writeFile(path.join(setup.paths.profile, 'test-marker'), 'test');
  await fs.writeFile(path.join(sibling, 'test-marker'), 'preserve');
  await fs.writeFile(setup.paths.usage, '{}');
  assert.equal(await runReset(setup.paths, { log: setup.log, isProfileInUse: () => false }), 0);
  await assert.rejects(fs.access(setup.paths.profile), { code: 'ENOENT' });
  await assert.rejects(fs.access(setup.paths.usage), { code: 'ENOENT' });
  assert.equal(await fs.readFile(path.join(sibling, 'test-marker'), 'utf8'), 'preserve');
  assert.equal(JSON.parse(await fs.readFile(setup.paths.status, 'utf8')).state, 'reset');
  for (const key of ['watchLock', 'runLock']) await assert.rejects(fs.access(setup.paths[key]), { code: 'ENOENT' });
});

test('reset refuses an active operation, open profile, and unexpected profile path', async (t) => {
  const setup = await fixture(t);
  await fs.mkdir(setup.paths.profile);
  await fs.writeFile(path.join(setup.paths.profile, 'test-marker'), 'preserve');
  const lock = await acquireLock(setup.paths.runLock, 'once');
  assert.equal(await runReset(setup.paths, { log: setup.log, isProfileInUse: () => false }), 2);
  await lock.release();
  assert.equal(await runReset(setup.paths, { log: setup.log, isProfileInUse: () => true }), 1);
  const unexpected = { ...setup.paths, profile: path.join(setup.paths.base, 'claude-browser-profile') };
  assert.equal(await runReset(unexpected, { log: setup.log, isProfileInUse: () => false }), 1);
  assert.equal(await fs.readFile(path.join(setup.paths.profile, 'test-marker'), 'utf8'), 'preserve');
});

test('reset refuses a directory junction/symlink instead of traversing it', async (t) => {
  const setup = await fixture(t);
  const other = path.join(setup.paths.base, 'other-profile');
  await fs.mkdir(other);
  await fs.writeFile(path.join(other, 'test-marker'), 'preserve');
  await fs.symlink(other, setup.paths.profile, process.platform === 'win32' ? 'junction' : 'dir');
  assert.equal(await runReset(setup.paths, { log: setup.log, isProfileInUse: () => false }), 1);
  assert.equal(await fs.readFile(path.join(other, 'test-marker'), 'utf8'), 'preserve');
});
