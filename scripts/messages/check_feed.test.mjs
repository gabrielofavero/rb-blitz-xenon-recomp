import { test } from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, readFileSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const validator = join(root, 'scripts/messages/check_feed.mjs');
const canonical = JSON.parse(readFileSync(join(root, 'assets/messages/feed.json'), 'utf8'));

function fixture(run) {
  const directory = mkdtempSync(join(tmpdir(), 'rbblitz-feed-'));
  try {
    const feed = structuredClone(canonical);
    const release = {
      tag_name: `v${feed.latest_version}`, draft: false, prerelease: false,
      assets: [{ name: `RockBandBlitz-${feed.latest_version}-windows-amd64.exe`, size: 123 }],
    };
    const feedPath = join(directory, 'feed.json');
    const releasePath = join(directory, 'release.json');
    const check = (withRelease = false) => {
      writeFileSync(feedPath, JSON.stringify(feed));
      writeFileSync(releasePath, JSON.stringify(release));
      const args = [validator, '--feed', feedPath, '--assets-dir', directory];
      if (withRelease) args.push('--release-json', releasePath);
      return spawnSync(process.execPath, args, { encoding: 'utf8' });
    };
    run({ feed, release, check, releasePath, directory });
  } finally { rmSync(directory, { recursive: true, force: true }); }
}

test('shipped feed matches the pinned build', () => {
  fixture(({ check }) => assert.equal(check().status, 0));
});

test('local icon checks size and name, then refuses an undrawable image', () => {
  fixture(({ feed, check, directory }) => {
    feed.messages[0].icon = 'Mascot.PNG';
    writeFileSync(join(directory, 'mascot.png'), Buffer.from([0x89, 0x50, 0x4e, 0x47]));
    let result = check();
    assert.equal(result.status, 1);
    assert.match(result.stderr, /cannot be rendered by the current inbox row material binding/);
    assert.doesNotMatch(result.stderr, /file missing/);

    writeFileSync(join(directory, 'mascot.png'), Buffer.alloc(1024 * 1024 + 1));
    result = check();
    assert.equal(result.status, 1);
    assert.match(result.stderr, /larger than 1 MiB/);

    feed.messages[0].icon = '../mascot.png';
    result = check();
    assert.equal(result.status, 1);
    assert.match(result.stderr, /unknown or unsafe icon/);
  });
});

test('missing feed and feed version drift refuse packaging', () => {
  fixture(({ feed, check }) => {
    feed.latest_version = '0.2.0';
    const mismatch = check();
    assert.equal(mismatch.status, 1);
    assert.match(mismatch.stderr, /does not match \[installer\] version/);
    const missing = spawnSync(process.execPath,
      [validator, '--feed', join(tmpdir(), 'rbblitz-feed-does-not-exist.json')],
      { encoding: 'utf8' });
    assert.equal(missing.status, 1);
    assert.match(missing.stderr, /feed:/);
  });
});

test('unsafe message and missing local icon refuse publication', () => {
  fixture(({ feed, check }) => {
    feed.messages[0].id = '../unsafe';
    feed.messages[0].icon = 'absent.png';
    const result = check();
    assert.equal(result.status, 1);
    assert.match(result.stderr, /invalid id/);
    assert.match(result.stderr, /file missing/);
  });
});

test('missing release refuses publication', () => {
  fixture(({ check, releasePath }) => {
    rmSync(releasePath, { force: true });
    // The helper writes fixtures on each call, so ask the CLI directly here.
    const result = spawnSync(process.execPath,
      [validator, '--release-json', releasePath], { encoding: 'utf8' });
    assert.equal(result.status, 1);
    assert.match(result.stderr, /release:/);
  });
});

test('matching release and setup asset permit publication', () => {
  fixture(({ check }) => assert.equal(check(true).status, 0));
});

test('missing setup asset and wrong release version refuse publication', () => {
  fixture(({ release, check }) => {
    release.assets = [];
    let result = check(true);
    assert.equal(result.status, 1);
    assert.match(result.stderr, /missing nonempty setup asset/);
    release.assets = [{ name: 'RockBandBlitzSetup-0.1.0.exe', size: 123 }];
    result = check(true);
    assert.equal(result.status, 1);
    assert.match(result.stderr, /missing nonempty setup asset/);
    release.assets = [{ name: `RockBandBlitz-${release.tag_name.slice(1)}-windows-amd64.exe`, size: 123 }];
    release.tag_name = 'v0.2.0';
    result = check(true);
    assert.equal(result.status, 1);
    assert.match(result.stderr, /does not match feed version/);
  });
});
