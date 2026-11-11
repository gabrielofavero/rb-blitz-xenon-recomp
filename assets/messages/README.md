# Messages feed

`feed.json` is the canonical feed for the game. The installer copies this same
file to `assets/messages/feed.json` beside the game executable. GitHub Pages
publishes only the JSON at
`https://gabrielofavero.github.io/rb-blitz-xenon-recomp/feed.json` after the
matching setup release exists. No icon image is hosted in the feed.

Edit the JSON and run `node scripts/messages/check_feed.mjs` from the repository
root. The file must contain at least one message, be newest first, and use the
schema and icon names in
[`docs/plans/finished/2026-10-23-messages-and-update-feed-plan.md`](../../docs/plans/finished/2026-10-23-messages-and-update-feed-plan.md).
The optional `notification` field is the short label beside the icon in the
left list, preceded by the published date. The right detail heading uses
`title`; when `notification` is absent, the list uses `title` too.
The setup build also runs this validator and refuses a feed version that differs
from `[installer] version` in `installer/config/pins.toml`.
PNG/JPEG files with safe local names and size at most 1 MiB are copied into the
setup payload beside the feed and audited against the source files. The current
inbox row hook selects only the game's built-in materials; local image files
cannot yet draw as message icons. The validator therefore refuses a feed that
names one for publication, even if the file itself is valid. Such a name in a
downloaded feed draws `icon_misc` as a safe fallback.

The Pages workflow checks the latest published GitHub release and its
`RockBandBlitz-<version>-windows-amd64.exe` asset before publishing. After a release,
message-only edits can be dispatched manually while `latest_version` remains
the latest published setup version. GitHub Pages must be configured to use
GitHub Actions as its publishing source; this repository change does not enable
the Pages setting or publish the first release.
