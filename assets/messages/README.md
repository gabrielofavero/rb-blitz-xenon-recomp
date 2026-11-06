# Messages feed

`feed.json` is the canonical feed for the game. The installer copies this same
file to `assets/messages/feed.json` beside the game executable. GitHub Pages
publishes only the JSON at
`https://gabrielofavero.github.io/rb-blitz-xenon-recomp/feed.json` after the
matching setup release exists. No icon image is hosted in the feed.

Edit the JSON and run `node scripts/messages/check_feed.mjs` from the repository
root. The file must contain at least one message, be newest first, and use the
schema and icon names in
[`docs/plans/todo/2026-10-23-messages-and-update-feed-plan.md`](../../docs/plans/todo/2026-10-23-messages-and-update-feed-plan.md).
The setup build also runs this validator and refuses a feed version that differs
from `[installer] version` in `installer/config/pins.toml`.

The Pages workflow checks the latest published GitHub release and its
`RockBandBlitzSetup-<version>.exe` asset before publishing. After a release,
message-only edits can be dispatched manually while `latest_version` remains
the latest published setup version. GitHub Pages must be configured to use
GitHub Actions as its publishing source; this repository change does not enable
the Pages setting or publish the first release.
