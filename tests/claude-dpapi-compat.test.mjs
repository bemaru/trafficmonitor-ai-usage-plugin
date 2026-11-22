import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import fs from 'node:fs/promises';
import path from 'node:path';
import test from 'node:test';
import vm from 'node:vm';

test('Claude master-key decryption works in Windows PowerShell 5.1', {
  skip: process.platform !== 'win32' ? 'Windows DPAPI requires Windows.' : false,
}, async () => {
  const powershell = path.join(process.env.SystemRoot || 'C:\\Windows',
    'System32', 'WindowsPowerShell', 'v1.0', 'powershell.exe');
  function runPowerShell51(script) {
    const versionCheck = "$ErrorActionPreference = 'Stop'; "
      + "if ($PSVersionTable.PSEdition -ne 'Desktop' -or $PSVersionTable.PSVersion.Major -ne 5 "
      + "-or $PSVersionTable.PSVersion.Minor -ne 1) { throw 'Windows PowerShell 5.1 required' }; ";
    const encodedCommand = Buffer.from(versionCheck + script, 'utf16le').toString('base64');
    const result = spawnSync(powershell, ['-NoProfile', '-NonInteractive', '-EncodedCommand', encodedCommand], {
      encoding: 'utf8',
      windowsHide: true,
      timeout: 15_000,
      maxBuffer: 16_384,
    });
    // Keep captured process output private even when an assertion fails.
    assert.ok(!result.error && result.status === 0, 'Windows PowerShell 5.1 DPAPI operation succeeds');
    return result.stdout.trim();
  }

  // This is a public test fixture, never a browser key. The encrypted fixture
  // exists only in memory and is protected under the current test user.
  const fixtureKey = Buffer.from('trafficmonitor-test-master-key', 'utf8');
  const protectedFixture = runPowerShell51(
    'Add-Type -AssemblyName System.Security; '
    + "$bytes = [Text.Encoding]::UTF8.GetBytes('trafficmonitor-test-master-key'); "
    + '[Convert]::ToBase64String([Security.Cryptography.ProtectedData]::Protect('
    + '$bytes, $null, [Security.Cryptography.DataProtectionScope]::CurrentUser))',
  );
  const encryptedKey = Buffer.concat([
    Buffer.from('DPAPI', 'utf8'),
    Buffer.from(protectedFixture, 'base64'),
  ]).toString('base64');

  const source = await fs.readFile(new URL('../helper/claude-web-helper/index.mjs', import.meta.url), 'utf8');
  const masterKeySource = source.match(/^function getMasterKey\(\) \{[\s\S]*?^\}/m)?.[0];
  assert.ok(masterKeySource, 'the actual Claude helper getMasterKey function is found');
  let decryptionCalls = 0;
  const context = vm.createContext({
    Buffer,
    cachedMasterKey: null,
    getLocalStatePayload: () => ({ os_crypt: { encrypted_key: encryptedKey } }),
    runPowerShell: (script) => {
      decryptionCalls += 1;
      return runPowerShell51(script);
    },
  });
  // Evaluate the original function unchanged so the source's conditional
  // System.Security assembly load is exercised in a fresh 5.1 process.
  vm.runInContext(masterKeySource, context);
  const first = vm.runInContext('getMasterKey()', context);
  assert.ok(Buffer.isBuffer(first) && first.equals(fixtureKey), 'the public fixture decrypts correctly');
  const second = vm.runInContext('getMasterKey()', context);
  assert.ok(second === first, 'subsequent reads reuse the in-memory master key');
  assert.equal(decryptionCalls, 1, 'cached reads do not run another PowerShell process');
});
