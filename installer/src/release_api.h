// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The GitHub release API, read without a JSON library.
//
// The `latest` setup executable is a downloader (README.md, section "The latest
// build"): it asks GitHub what the newest release of this repository is, and what
// the one file on it is, then downloads that file and runs it. Answering that
// question needs four values out of the release payload - the tag, and the
// asset's name, URL, size and digest - so this header is a deliberate,
// narrow reader rather than a JSON parser: it extracts the fields the release
// resource is documented to have, refuses a document it cannot read, and gives
// the reason.
//
// Two rules make that safe:
//
//   * nothing is guessed from position. A field is found by its key, scanning
//     around string literals so a key inside a value ("..." : "tag_name") cannot
//     be mistaken for the field itself.
//   * a missing `digest` is reported as missing rather than treated as a
//     mismatch. GitHub publishes `sha256:` digests for release assets; a release
//     made before it did, or by a client that did not send one, is still
//     installable with the size check alone - loudly, which is why the flag
//     exists instead of an empty string.
//
// It is header-and-source rather than one file because the parser is the part
// worth testing: no process, no network, no game image - see
// tests/installer_tests.cpp.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace rb_blitz::installer {

// One release, as far as this installer needs to know it.
struct LatestRelease {
  // `tag_name` from the release resource. Recorded, never parsed for meaning:
  // which channel a build belongs to is GitHub's own `prerelease` flag, not the
  // spelling of its tag, so a tag can be renamed without changing what a
  // bootstrap installs.
  std::string tag_name;
  // GitHub's marker for "published, but not a release": a build of main. The
  // `nightly` bootstrap is the one that installs these, and the `latest`
  // bootstrap refuses one handed to it by mistake.
  bool prerelease = false;
  // How many files the release carries. The release model is one file per
  // release, and a release with several is refused unless the caller said which
  // one it wants - taking "the first" would be picking at random.
  int asset_count = 0;
  std::string asset_name;
  std::string asset_url;
  std::uint64_t asset_size = 0;
  // Empty when the release published no digest; `digest_published` tells the two
  // cases apart.
  std::string asset_sha256;
  bool digest_published = false;
};

// Which of the three products a bootstrap is asking for (README.md, section
// "The three products").
enum class ReleaseChannel {
  // The newest release: what `GET /releases/latest` answers, which GitHub
  // resolves to the newest published release that is *not* a prerelease. This is
  // the `latest` bootstrap.
  kRelease,
  // The newest build of main: the newest release GitHub marks as a prerelease.
  // This is the `nightly` bootstrap, and it is the only one that installs a
  // build which has not been released.
  kNightly,
};

// `release`/`latest` and `nightly`, as the `--channel` option and the build
// script spell them. Returns false for anything else.
bool ParseReleaseChannel(std::string_view text, ReleaseChannel* out);
// `latest` or `nightly`: the name of the product, which is also the suffix of
// its setup executable (`RockBandBlitzSetup-latest.exe`).
const char* ReleaseChannelName(ReleaseChannel channel);
// One sentence for a progress page: what this channel installs.
std::string ReleaseChannelDescription(ReleaseChannel channel);

// Reads the release resource into `out`. Returns false and sets `*error` for a
// document that is not a release, has no `tag_name`, carries no asset, or names
// an asset a downloader cannot fetch (no https URL, no size).
bool ParseLatestReleaseJson(std::string_view json, LatestRelease* out, std::string* error);

// Reads an array of release resources - what `GET /releases` answers - and takes
// the newest one GitHub marks as a prerelease: the newest build of main. A
// release that cannot be installed is skipped and remembered, so one broken
// nightly does not hide the build before it, and the error names the newest one
// that failed rather than only "none found".
bool ParseNewestNightlyJson(std::string_view json, LatestRelease* out, std::string* error);

// The API URL for the newest release of the repository `homepage_url` names, or
// empty when that URL is not a github.com repository. `https://github.com/x/y`
// and `.../x/y.git` both give `https://api.github.com/repos/x/y/releases/latest`.
//
// Derived rather than pinned a second time: the repository coordinates already
// live in config/pins.toml as the installer's homepage, and a second copy is one
// more place for the two to disagree (README.md, section "Updating").
std::string LatestReleaseApiUrl(std::string_view homepage_url);

// The same for the newest build of main: `.../releases?per_page=30`, because the
// newest prerelease has to be picked from the list - GitHub has no "newest
// prerelease" endpoint. A page of them, so a broken nightly or two does not hide
// the newest usable one.
std::string NewestNightlyApiUrl(std::string_view homepage_url);

// Which URL answers `channel`. The one place that maps a channel onto an
// endpoint, so a bootstrap and the helper it runs cannot ask two different
// questions.
std::string ReleaseApiUrlFor(ReleaseChannel channel, std::string_view homepage_url);

// Where the newest commit of the default branch is: `.../commits/main`. The nightly
// channel asks this as well as the release list, because "the newest build of main"
// and "the newest commit of main" are two different answers the moment main moves
// ahead of what has been built - and a nightly build should say which of the two it
// installed.
std::string NewestMainCommitApiUrl(std::string_view homepage_url);

// The same question, asked of the URL the channel was *actually* asked: everything
// before the trailing `/releases...` plus `/commits/main`. This is what keeps a run
// pointed at one server (--api, the /LATEST_API= test hook) pointed at that server for
// both questions - deriving it from the homepage instead sent the commit question to
// GitHub while the release came from the mock, so a mock could not fail a comparison
// it was built to fail (found by scripts/acceptance_bootstrap.ps1, leg 5). Empty when
// the URL names no `/releases` path to replace.
std::string CommitApiUrlFrom(std::string_view channel_api_url);

// The `sha` of a commit resource, from `GET .../commits/main`.
bool ParseMainCommitSha(std::string_view json, std::string* out, std::string* error);

// The commit a release names, read out of its tag or its file name: the last
// maximal run of hex digits at least seven long. It is a convention, not a promise -
// a tag is free text - so a release that names no commit yields an empty string and
// the caller says "the build names no commit" rather than guessing.
//
// The *last* run, because a tag like nightly-2026-10-15-96dce40 carries a date whose
// parts are hex digits too, and only the end of it is the commit.
std::string CommitMarkerFromName(std::string_view name);

// Whether `url` is a plain-http URL naming this machine itself: 127.0.0.1,
// localhost or [::1], with or without a port. A release's asset must be served
// over https, without exception - the one thing this admits is a release served by
// a server on this machine, which is how the whole path (ask, download, verify,
// run) is exercised without publishing a release to exercise it with. GitHub never
// serves an asset from a loopback address, so this cannot loosen a real install.
bool IsLoopbackHttpUrl(std::string_view url);

// Whether `text` is 64 lower-case hex characters (the shape GitHub's
// `sha256:` digests carry once the prefix is removed). Used to refuse a digest
// that is not one before it is compared with a download.
bool IsHexSha256(std::string_view text);

}  // namespace rb_blitz::installer
