# Native aspect resolution policy

Date: 2026-11-18. Status: finished implementation; owner runtime acceptance pending.

## Deliverables

- D1: Investigate the song-start crash recorded after resolution changes.
- D2: Offer only the display's proportions for Interface and Gameplay resolution,
  keeping resolution changes immediate and preserving selected quality tiers.
- D3: Add a desktop Force 16:9 preference, default off. Use the existing restart
  prompt if changing loaded scene proportions cannot be made reliable live.
- D4: Build, run focused checks, update current behavior records and document
  remaining owner acceptance without navigating the game.

## Evidence

- 2026-11-18 [owner report]: aspect changes still do not take effect; entering
  gameplay crashes after menu resolution changes.
- 2026-11-18 [log]: `out/build/win-amd64-release/logs/rb_blitz_008.log`
  records a guest fault during loading after repeated profile replacements.
  The matching release mapping and local crash dump identify UIColor teardown,
  `Hmx::Object` destructor `sub_823FCFC8` calling `sub_823FC660`, reading an invalid
  child pointer `0x981F` at object `0x4081DC08` +16. The dump does not capture the
  original corrupting write; suppressing the destructor would hide the damage.

## Implementation

- D1 [static]: native SetTexture `sub_825087A8` caches borrowed pointers at
  device +12920 + stage*4 and later writes through the old resource +8. All 26
  slots, including vertex samplers, now replace matching old textures with
  fresh equivalents before release. Native dirty masks are preserved.
- D1 [static]: RndTex wrappers at renderer +824/+828/+832 borrow textures from
  +816/+820/+760 without AddRef. Native `sub_823ED1D8` now refreshes these existing
  wrapper objects and their width, height and format before old resources are
  released. No object identities or external material references are replaced.
  Failed allocations or initialization leave old bindings and resources intact.
- D2 [implemented]: resolution lists contain only native-aspect tiers; native
  keeps its stable token, and old explicit requests reconcile by height without
  rewriting saved intent. Recovery uses the active aspect's lowest tier. Native
  render targets remain hot, with tile capture before drawing and window size
  independent of render size. Aspect literals stay fixed across pixel rounding.
- D3 [implemented]: desktop Force 16:9 is stable ID 33, stored as
  `graphics.force_16_by_9`, default false, third Resolution row. It uses the
  existing restart prompt; the next boot converts both lists to fitted 16:9.
  Current lists and scene proportions remain unchanged until restart. Mobile
  hides the preference and retains the independent render-scale wheels.
- D3 [rationale]: log008 shows successful ratio writes, yet the owner still
  observes no correct live aspect change. Existing scenes are not proven to
  rebuild reliably. The unverified dynamic camera refresh hooks are retired;
  changing proportions is restart-scoped, while changing pixel density is hot.
- D3 [integration]: Vanilla restore stages the current aspect's 720p tier, then
  converts it to 1280x720 after the forced-aspect restart. Last-good graphics
  recovery adopts its working aspect before rebuilding descriptors and pixels.
- Pause selection correction: inline Help & Options retains `game_screen` and
  `pause_panel` but focuses `options.lst`; it now selects Interface there and
  Gameplay on the native `pause.lst`. Nested pages and song confirmations retain
  their respective Interface and Gameplay policies.
- Scope: shared title-side code for supported platforms and both GPU backends.
  No SDK patches, APIs or backend defaults change.

## Validation

- D4 [build]: `cmake --build --preset win-amd64-release` passed on Windows AMD64.
  Evidence: `out/native-aspect-resolution-build.log`, incremental
  `out/native-aspect-resolution-build-final.log`, and final
  `out/native-aspect-resolution-build-verified.log` after the restore, recovery
  and inline pause corrections.
- D4 [host tests]: all 15 focused CTests passed after the final build in 16.80s;
  evidence: `out/native-aspect-resolution-tests-final.log`. Coverage includes
  resolution tiers and conversion, persistence and restart behavior, mobile
  pruning, Force 16:9 menu writes, all 26 borrowed sampler slots and wrapper
  mappings, recovery aspect adoption, Vanilla restore across restart, pause's
  inline options route, and existing settings/window/payload checks.
- D4 [static review]: native wrapper/sampler signatures, dirty masks, binding
  lifetimes and nonpublishing allocation failures match the generated retail
  implementation. The texture clear extent matches native initialization.
- D4 [documentation]: references updated and the follow-up moved to finished.
  Final checker evidence is `out/native-aspect-resolution-docs.log`.
- [unverified]: repeated hot resolution changes followed by song entry, crash-free
  gameplay, correct proportions and bars after Force 16:9 restart, and all pause
  transitions. The crash's original corrupting write is not proven by the dump.
  No game/UI navigation was performed; owner runtime acceptance remains required.
  Linux, macOS, iOS, Android and MoltenVK builds/runs were not tested on this host.
