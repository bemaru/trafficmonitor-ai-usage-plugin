import assert from 'node:assert/strict';
import { EventEmitter } from 'node:events';
import { PassThrough } from 'node:stream';
import test from 'node:test';
import { ChromePipe } from './browser.mjs';

function fakeBrowser() {
  const child = new EventEmitter();
  child.stdio = [null, null, null, new PassThrough(), new PassThrough()];
  child.kill = () => { child.exitCode = 0; child.emit('exit', 0); };
  child.exitCode = null;
  return child;
}

test('pipe protocol reassembles fragmented null-delimited CDP replies', async () => {
  const child = fakeBrowser();
  const pipe = new ChromePipe(child);
  let sent;
  child.stdio[3].on('data', (data) => { sent = JSON.parse(data.toString().slice(0, -1)); });
  const request = pipe.send('Browser.getVersion');
  assert.equal(sent.method, 'Browser.getVersion');
  const reply = `${JSON.stringify({ id: sent.id, result: { product: 'Chrome/test' } })}\0`;
  child.stdio[4].write(reply.slice(0, 12));
  child.stdio[4].write(reply.slice(12));
  assert.deepEqual(await request, { product: 'Chrome/test' });
  child.kill();
  await pipe.close();
});

test('CDP errors are reduced to a safe state without server exception text', async () => {
  const child = fakeBrowser();
  const pipe = new ChromePipe(child);
  child.stdio[3].on('data', (data) => {
    const command = JSON.parse(data.toString().slice(0, -1));
    child.stdio[4].write(`${JSON.stringify({ id: command.id, error: { message: 'Bearer PRIVATE-SECRET' } })}\0`);
  });
  await assert.rejects(pipe.send('Runtime.evaluate'), (error) => {
    assert.equal(error.state, 'browser_protocol_failed');
    assert.doesNotMatch(error.message, /PRIVATE|Bearer/);
    return true;
  });
  child.kill();
  await pipe.close();
});

test('a closed browser rejects outstanding requests and clears pending timers', async () => {
  const child = fakeBrowser();
  const pipe = new ChromePipe(child);
  const request = pipe.send('Runtime.evaluate');
  child.kill();
  await assert.rejects(request, (error) => error.state === 'browser_closed');
  assert.equal(pipe.pending.size, 0);
  await assert.rejects(pipe.send('Browser.getVersion'), (error) => error.state === 'browser_closed');
});

test('bounded command timeout removes the pending request', async () => {
  const child = fakeBrowser();
  const pipe = new ChromePipe(child);
  await assert.rejects(pipe.send('Browser.getVersion', {}, undefined, 5), (error) => error.state === 'browser_timeout');
  assert.equal(pipe.pending.size, 0);
  child.kill();
  await pipe.close();
});

test('graceful close waits for profile flush/exit and shares its completion promise', async () => {
  const child = fakeBrowser();
  let killed = false;
  child.kill = () => { killed = true; child.exitCode = 1; child.emit('exit', 1); };
  const pipe = new ChromePipe(child);
  child.stdio[3].on('data', (data) => {
    const command = JSON.parse(data.toString().slice(0, -1));
    if (command.method === 'Browser.close') {
      child.stdio[4].write(`${JSON.stringify({ id: command.id, result: {} })}\0`);
      setTimeout(() => { child.exitCode = 0; child.emit('exit', 0); }, 20);
    }
  });
  const first = pipe.close();
  const second = pipe.close();
  assert.equal(first, second);
  assert.equal(child.exitCode, null);
  await first;
  assert.equal(child.exitCode, 0);
  assert.equal(killed, false);
});
