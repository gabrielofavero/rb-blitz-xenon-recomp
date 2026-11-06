#!/usr/bin/env node
// Validate the one feed shipped in the setup and published through GitHub Pages.
// With --release-json, compare against the latest published release instead of
// the checkout's pins, which may already have been bumped for the next build.
import { readFileSync, readdirSync, lstatSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const builtInIcons = new Set([
  'icon_challenge', 'icon_friend_popular', 'icon_friends_play', 'icon_misc',
  'icon_more_stars', 'icon_new_release', 'icon_revisit_song', 'icon_store_plays',
  'icon_tournament', 'icons_decrease_rank', 'icons_increase_rank',
  'icons_store_rec', 'icons_unplayed_songs',
]);

function option(args, name, fallback) {
  const index = args.indexOf(name);
  if (index < 0) return fallback;
  if (index + 1 >= args.length || args[index + 1].startsWith('--')) {
    throw new Error(`${name} requires a path`);
  }
  return resolve(args[index + 1]);
}

function readJson(path, label, problems) {
  try { return JSON.parse(readFileSync(path, 'utf8')); }
  catch (error) { problems.push(`${label}: ${error.message}`); return null; }
}

function validDate(value) {
  if (typeof value !== 'string' || !/^\d{4}-\d{2}-\d{2}$/.test(value)) return false;
  const date = new Date(`${value}T00:00:00Z`);
  return !Number.isNaN(date.valueOf()) && date.toISOString().slice(0, 10) === value;
}

function validVersion(value) {
  return typeof value === 'string' && /^\d{1,5}\.\d{1,5}\.\d{1,5}$/.test(value);
}

function visibleLength(value, limit, allowNewline = false) {
  if (typeof value !== 'string') return false;
  const count = Array.from(value).length;
  const hasControl = Array.from(value).some(character =>
    !(allowNewline && character === '\n') && /\p{Cc}/u.test(character));
  return count >= 1 && count <= limit && value.trim().length > 0 && !hasControl;
}

function checkIcon(icon, assetsDir, problems, label) {
  if (icon === undefined) return;
  if (typeof icon !== 'string') { problems.push(`${label}: icon must be a string`); return; }
  if (builtInIcons.has(icon)) return;
  if (!/^[a-z0-9][a-z0-9_-]{0,63}\.(?:png|jpe?g)$/i.test(icon)) {
    problems.push(`${label}: unknown or unsafe icon ${JSON.stringify(icon)}`);
    return;
  }
  try {
    const matches = readdirSync(assetsDir).filter(name => name.toLowerCase() === icon.toLowerCase());
    if (matches.length !== 1) throw new Error(matches.length ? 'ambiguous case-insensitive name' : 'file missing');
    const info = lstatSync(join(assetsDir, matches[0]));
    if (!info.isFile() || info.size > 1024 * 1024) throw new Error('not a file or larger than 1 MiB');
  } catch (error) { problems.push(`${label}: icon ${icon}: ${error.message}`); }
}

function checkFeed(feed, assetsDir, problems) {
  if (feed === null || typeof feed !== 'object' || Array.isArray(feed)) {
    problems.push('feed: expected a JSON object'); return;
  }
  if (feed.schema_version !== 1) problems.push('schema_version: expected integer 1');
  if (!validVersion(feed.latest_version)) problems.push('latest_version: expected MAJOR.MINOR.PATCH, each component at most five digits');
  if (typeof feed.generated_at !== 'string' ||
      !/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d+)?Z$/.test(feed.generated_at) ||
      Number.isNaN(Date.parse(feed.generated_at)) ||
      new Date(feed.generated_at).toISOString().slice(0, 19) !== feed.generated_at.slice(0, 19)) {
    problems.push('generated_at: expected an RFC 3339 UTC timestamp');
  }
  if (!Array.isArray(feed.messages) || feed.messages.length < 1 || feed.messages.length > 32) {
    problems.push('messages: expected 1..32 entries in the shipped feed'); return;
  }
  const ids = new Set();
  let previousDate = '9999-99-99';
  for (const [index, message] of feed.messages.entries()) {
    const label = `messages[${index}]`;
    if (message === null || typeof message !== 'object' || Array.isArray(message)) {
      problems.push(`${label}: expected an object`); continue;
    }
    if (typeof message.id !== 'string' || !/^[a-z0-9][a-z0-9-]{0,63}$/.test(message.id)) {
      problems.push(`${label}: invalid id`);
    } else if (ids.has(message.id)) {
      problems.push(`${label}: duplicate id ${message.id}`);
    } else ids.add(message.id);
    if (!visibleLength(message.title, 64)) problems.push(`${label}: title must have 1..64 visible characters`);
    if (!visibleLength(message.body, 1024, true)) problems.push(`${label}: body must have 1..1024 visible characters`);
    if (!validDate(message.published)) problems.push(`${label}: invalid published date`);
    else {
      if (message.published > previousDate) problems.push(`${label}: messages must be newest first`);
      previousDate = message.published;
    }
    checkIcon(message.icon, assetsDir, problems, label);
  }
}

function pinnedVersion(path, problems) {
  try {
    let inInstaller = false;
    for (const line of readFileSync(path, 'utf8').split(/\r?\n/)) {
      const section = /^\s*\[([^\]]+)\]\s*$/.exec(line);
      if (section) { inInstaller = section[1] === 'installer'; continue; }
      if (!inInstaller) continue;
      const version = /^\s*version\s*=\s*"([^"]+)"\s*$/.exec(line)?.[1];
      if (validVersion(version)) return version;
    }
    problems.push('pins: missing or invalid [installer] version');
  } catch (error) { problems.push(`pins: ${error.message}`); }
  return null;
}

function checkRelease(feed, release, problems) {
  if (release === null || typeof release !== 'object' || Array.isArray(release)) {
    problems.push('release: expected the latest published release object'); return;
  }
  if (release.draft || release.prerelease) problems.push('release: draft or prerelease is not an installable latest release');
  const version = feed?.latest_version;
  if (!validVersion(version)) return;
  if (release.tag_name !== version && release.tag_name !== `v${version}`) {
    problems.push(`release: tag ${JSON.stringify(release.tag_name)} does not match feed version ${version}`);
  }
  const asset = `RockBandBlitzSetup-${version}.exe`;
  if (!Array.isArray(release.assets) || !release.assets.some(item => item?.name === asset && item?.size > 0)) {
    problems.push(`release: missing nonempty setup asset ${asset}`);
  }
}

function main(args) {
  const problems = [];
  let feedPath, assetsDir, pinsPath, releasePath;
  try {
    feedPath = option(args, '--feed', join(root, 'assets/messages/feed.json'));
    assetsDir = option(args, '--assets-dir', join(root, 'assets/messages'));
    pinsPath = option(args, '--pins', join(root, 'installer/config/pins.toml'));
    releasePath = option(args, '--release-json', null);
    const known = new Set(['--feed', '--assets-dir', '--pins', '--release-json']);
    for (let i = 0; i < args.length; i += 2) {
      if (!known.has(args[i]) || i + 1 >= args.length) throw new Error(`unknown or incomplete option ${args[i]}`);
    }
  } catch (error) { console.error(`feed: ${error.message}`); return 1; }
  let feed = null;
  try {
    const bytes = readFileSync(feedPath);
    if (bytes.length > 256 * 1024) problems.push('feed: larger than the game transport limit of 256 KiB');
    feed = JSON.parse(bytes.toString('utf8'));
  } catch (error) { problems.push(`feed: ${error.message}`); }
  checkFeed(feed, assetsDir, problems);
  if (releasePath) checkRelease(feed, readJson(releasePath, 'release', problems), problems);
  else if (feed && pinnedVersion(pinsPath, problems) !== feed.latest_version) {
    problems.push('latest_version: does not match [installer] version');
  }
  for (const problem of problems) console.error(problem);
  if (problems.length) return 1;
  console.log(`feed: valid ${feed.messages.length} message(s), version ${feed.latest_version}`);
  return 0;
}

process.exitCode = main(process.argv.slice(2));
