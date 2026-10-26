# In-game song management — implementation plan

**Status:** Planning only, 2026-10-26. No feature implementation, build or game/UI
testing in this pass. Findings below come from static inspection of the current
working tree, including the ongoing load-recovery changes. Guest navigation,
play-count availability and provider filtering still require tracing.

## 1. Request and scope

Add an always-available **Manage Songs** flow to the song library reached through
**Play → My Songs**. Players can hide any song or delete its installed DLC
package without finding it in RhythmVerse first. Reuse the guest's song-info
styling, buttons, footer and confirmation dialogs. No new host overlay is
authorized or needed, and no master feature toggle is added.

| Id | Requirement |
| --- | --- |
| R1 | Add **(X) Manage** to the My Songs footer. It opens management for the highlighted song, without playing it. The feature works offline and independently of the RhythmVerse enable switch. |
| R2 | The management panel uses the RhythmVerse song-info layout and shows song title, artist and source: **DLC Single**, **DLC Package: <name>**, or **Built-in Song**. |
| R3 | Show **Times Played** only if a reliable existing per-song count is available. Show an installation/addition date only for DLC, with truthful provenance; start recording dates for future installs and discovery. |
| R4 | Buttons, in order: **See Package**, **Hide**, **Delete**, **Cancel**. Hide See Package when it does not apply. Keep Delete visibly disabled for built-in content, with an explanation; enforce this restriction in the service too. |
| R5 | See Package opens the package's songs in the native library results/list presentation. It is a package-membership query, not an artist/title text search. |
| R6 | Hide requires confirmation, affects only the selected song, persists across launches and leaves its files and scores intact. It works for built-in songs and individual songs in a multi-song DLC package. |
| R7 | Delete requires confirmation. A multi-song DLC warning names the package and clearly says **all songs in that package will be deleted**, including hidden songs. Never surgically edit a container to remove one track. |
| R8 | Hidden songs disappear from ordinary library views. A default-**On** setting, **Hidden Songs in Search**, controls their inclusion in every library search route, including Ultimate search. Off excludes them there too. Hide confirmation copy follows the effective setting. |
| R9 | Add **Unhide All Songs** to settings with confirmation. It restores manual and automatic hides without reinstalling anything. Manage Songs itself is built in and cannot be toggled off. |
| R10 | Add **Auto-hide Failed Songs: On / Off**, default **On**, linked to load-guard recovery. On: after an eligible fatal song-load restart, hide the identified failed song before exposing the library and change the recovery message to say it was hidden only after success. Off: retain the current load-guard restart flow and existing popup text without automatically hiding the song. |
| R11 | Installed RhythmVerse offers show **Manage** instead of Delete and open this same flow. Cancel/Back returns to that offer's info panel, showing **Manage / Cancel** while still installed, rather than going to My Songs. |
| R12 | Preserve selection, sort, search/package filter and return context wherever possible. Changes must not expose stale playable entries or bypass the existing package/cache-refresh lifecycle. |

This pass plans the full retail and Ultimate routes. Preserve Demo/trial gates;
management must never unlock or expose otherwise unavailable content. Built-in
classification follows the actual content source, not a title-name heuristic.

## 2. Findings from the current tree

Inspected 2026-10-26. These are code observations, not runtime verification.

| Evidence | Finding and implication |
| --- | --- |
| [store_payload.cpp](../../../src/store/store_payload.cpp), `AddLibraryManagement` | The current guest info-panel transform supplies installed-state Delete, native confirmation/progress and a cache-refresh warning. Reuse the styling and dialog mechanisms, but replace the entry with shared management. |
| [store_library.h](../../../src/store/store_library.h), `LoadedSongIndex` | Store ownership matches normalized artist + title and can return several package paths. This is useful for ownership display, but insufficient authority for deleting one selected song's source. |
| [store_download_service.cpp](../../../src/store/store_download_service.cpp), `RunDelete` | Deletion currently gathers all matching paths plus a receipt path, removes containers or extracted directories, forgets indexed packages and tracks refresh needs. Refactor the useful worker/lifecycle pieces behind an exact package request; do not pass a synthetic store offer to delete arbitrary library songs. |
| [dlc_song_metadata.h](../../../src/fs/dlc_song_metadata.h), [menu_music_metadata.h](../../../src/hooks/menu_music_metadata.h) | Shared package reading exposes song key, stream, title and artist. It does not currently provide package display name, installation date or play count. Extend shared metadata carefully; avoid a second full package scan on the UI thread. |
| [dlc_cached_library.h](../../../src/fs/dlc_cached_library.h), [dlc_cache.h](../../../src/fs/dlc_cache.h) | Package enumeration has cache-hit and scan paths. Fingerprint modification times establish cache freshness, not installation time. Observation/date reconciliation must run on both paths and survive cache deletion. |
| [settings_screen.cpp](../../../src/ui/settings_screen.cpp) | **General → Interface → Music Library & Store** currently has two header switches. Adding the search preference, auto-hide preference and Unhide All action makes five rows, within the six-row page budget; Downloadable Content is narrower than this feature. |
| [menu_payload.cpp](../../../src/settings/menu_payload.cpp) | Served guest records already include song-library header transforms, locale generation, store management wiring and the native recovery-popup route. Integrate with this composition rather than replacing whole retail/Ultimate records. |
| [local_save.h](../../../src/fs/local_save.h), `ValidPayload` | The documented native save layout contains scores and stars, with no play counter in that layout. A high score is not a count; another authoritative runtime/profile source remains unproven. |
| [crash_recovery.cpp](../../../src/loading/crash_recovery.cpp), [song_load_monitor.cpp](../../../src/platform/song_load_monitor.cpp) | Fatal-load recovery currently carries a loading flag and one-shot recovered flag, but no failed-song identity. `ShouldRestart` accepts a nonzero process exit while loading; this does not establish that a song is corrupt. |
| [load_trace.cpp](../../../src/loading/load_trace.cpp), [crash_dialog_state.h](../../../src/loading/crash_dialog_state.h) | Load setup/completion/unload observations and a one-shot main-menu popup already exist. Capture identity before calling vulnerable setup; extend this lifecycle without inventing an independent restart system. |

Related work: [load-guard plan](../finished/2026-10-13-load-guard.md),
[background-music navigator](../in-progress/2026-10-22-background-music-navigator-plan.md),
and [library sorting](../../engine/song-library-sorting.md). Recheck these moving
implementations before changing their shared hooks or state.

## 3. Product decisions

| Id | Decision |
| --- | --- |
| D1 | Build one guest management screen backed by a store-independent library service. Reuse/derive the info-panel presentation without requiring a catalogue record, offer token, network request or enabled store. |
| D2 | Footer **X Manage** is scoped to a real selected song in My Songs and its library search/package-result views. It is unavailable on headings, empty lists, transitions and modal dialogs. Trace the real X action and interface remapping; do not assume X is `kAction_Option` (the current store documents that action as Y). |
| D3 | Use the existing settings path **Help & Options → General → Interface → Music Library & Store**. Add **Hidden Songs in Search: On / Off** and **Auto-hide Failed Songs: On / Off**, both default On, followed by **Unhide All Songs**. The latter is an action, not a boolean, and is unavailable with a clear reason when nothing is hidden. Auto-hide caption: “Hide a song after load guard restarts the game following a loading crash.” |
| D4 | Hide and Unhide All apply when returning to a safe library view; search preference commits rebuild the affected results at that boundary. Do not require a restart for visibility. If a native provider cannot safely refresh yet, complete that seam before shipping rather than silently persisting a nonfunctional preference. |
| D5 | Hidden status is independent of installed/owned status. RhythmVerse still marks a hidden installed song as installed and offers Manage. A found hidden song can be intentionally selected and played when search inclusion is On. |
| D6 | Package classification uses the complete validated package song set: one song means DLC Single, multiple means DLC Package. Prefer the package's authored display name, otherwise a safe filename-derived label, finally Unnamed Package. Do not infer a pack from a RhythmVerse offer title. |
| D7 | See Package appears for a resolved multi-song DLC package. It lists every member, including hidden members clearly marked Hidden, because this explicit inspection must show the full deletion scope. This is a dedicated package view, not a search exception that can leak hidden songs into ordinary/Ultimate search when Off. |
| D8 | Hide is per song, never per package. Existing hidden entries can be inspected from permitted search/package views; show a Hidden status and make repeated Hide idempotent. Individual Unhide and Hide Package are outside this request; recovery is through Unhide All. |
| D9 | Delete acts on one exact, validated installed package and its complete membership. If one displayed song resolves to multiple source packages, require a native source/package chooser before enabling destructive confirmation. Never delete every artist/title match automatically. |
| D10 | Hidden songs are excluded from automatic library picks and DLC menu-music rotation so hiding a failed song prevents automatic retry. Stop scheduling a newly hidden track, and retire its current preview/menu stream safely before any associated delete. Explicit search play remains allowed by D5. Scores/history remain stored and do not turn into a discovery path around the filter. |
| D11 | Keep song-management data under the resolved persistent user-data root, provisionally `song_management.json`, separate from disposable DLC caches, store receipts, settings values and the native save envelope. Hide membership is local to this installation/user-data root; existing guest profiles share it. |
| D12 | Do not add historical play-count tracking in this feature. If P0 cannot prove a trustworthy count with the correct song/profile semantics, omit Times Played entirely. Missing is different from a verified zero. |
| D13 | Auto-hide Failed Songs controls only the recovery hide and corresponding popup variant. It does not enable, disable or change load guard's monitor/restart eligibility. Read its effective committed value on the recovery boot before consuming the failed attempt; changes need no extra restart and apply to the next recovery decision. Off preserves existing hides and manual Hide, and cannot queue a deferred hide for when the setting is turned On later. If load-guard recovery is disabled or no eligible recovery occurs, this setting alone does nothing. |

### 3.1 Panel fields and confirmation copy

| Field | Display rule |
| --- | --- |
| Song / Artist | Use local selected-song metadata, with safe fallback text for absent fields. |
| Source | DLC Single; DLC Package: <name>; Built-in Song. Unknown or ambiguous source is explicit and disables Delete until resolved; it never disables Hide. |
| Times Played | Only a verified existing count; identify the active profile's scope if that is the count source. |
| Installed | DLC only, when a successful observed install established the time. |
| First Seen | DLC only, when discovery is the only evidence; use this label rather than claiming a historical install date. |
| Hidden | Show when applicable, including auto-hidden songs. |

Store UTC timestamps and format dates for the active locale. For preexisting DLC,
the first successful discovery establishes **First Seen**, not Installed. A
verified new RhythmVerse install records **Installed** at successful commit, never
at download start. Reconcile discovered packages after cache hits, cold scans,
manual installs and refreshes. Cache rebuilds, metadata refreshes and package
renames must not reset known dates. Do not use file creation/modified time as a
historical install claim. Built-in songs show neither date field.

Suggested native confirmation text (substitute escaped display names safely):

| Action | Text |
| --- | --- |
| Hide, search On | Hide “<song>” from your library? You can still find it using search. Restore hidden songs with Unhide All Songs in Help & Options → General → Interface → Music Library & Store. |
| Hide, search Off | Hide “<song>” from your library? Hidden songs will not appear in search while Hidden Songs in Search is Off. Restore them with Unhide All Songs in Help & Options → General → Interface → Music Library & Store. |
| Delete single | Delete “<song>” from this device? Its installed DLC package will be removed. |
| Delete package | Delete DLC package “<package>”? All <N> songs in this package will be deleted from this device, including “<song>” and any hidden songs. |
| Unhide All | Unhide all <N> hidden songs, including songs hidden after a loading failure? Songs that failed to load may fail again. |

Confirm buttons are **Hide / Cancel**, **Delete / Cancel**, or **Unhide All /
Cancel**. Back always cancels a pending confirmation. Wrap or paginate within the
guest's existing dialog layout; the explanation must not be clipped. Disabled
built-in Delete explains “Built-in songs cannot be deleted. You can hide them.”

### 3.2 Navigation and ownership

| Entry or action | Destination / return behavior |
| --- | --- |
| My Songs → X Manage | Capture selected identity, sort, scroll and filter context; open Manage. |
| Manage → Cancel / Back | Return to the captured library context. If the selected song vanished, choose a surviving neighbor or the native empty state. |
| Manage → See Package | Open package-member results with their own context; Back returns to the originating Manage panel. A member's X Manage nests a bounded return context for that member. |
| RhythmVerse info → Manage | Resolve the installed local song/package, capture the offer and catalogue context, and open the shared screen. No premature jump to My Songs. |
| Manage from RhythmVerse → Cancel / Back | Return to the same info panel and recompute its live ownership/buttons. Still installed means Manage / Cancel; successful deletion means the normal uninstalled action / Cancel. |
| Confirmation → Cancel / Back | Return to the same Manage panel without mutation. |
| Successful Hide / Delete | Complete persistence/refresh, then return through the originating context. Store routes refresh their info panel; library routes rebuild their current view. |

Use an explicit bounded context stack rather than a global last-screen flag.
Invalidate dead selections using catalog generations. Snapshot identity and source
when opening the panel and revalidate at confirmation; asynchronous refreshes must
not silently change the target or expand the confirmed package membership.

## 4. Data and runtime design

### 4.1 Identity and persistent state

Introduce a shared catalog snapshot mapping a stable song identity to guest song
key/id, source, exact package identity/path, package members and display metadata.
Built-in identity includes the resolved content family. DLC identity combines a
stable package/content identity with the package's song key; a path is a locator,
not the durable key. Artist/title is display/matching data, never a destructive
identifier. Trace how duplicate guest keys and alternate charts resolve before
choosing the final identity format; filter the actual resolved source.

Version the store and bound sizes/counts. Persist hide records (reason `manual` or
`load_failure`, first-hidden time), package dates and date provenance. Keep absent
package records so an offline drive or temporary removal does not erase hides or
dates. A genuinely new package identity must not inherit another chart's hide.
Define reinstall behavior explicitly: preserve the song's hide preference and
original First Seen, update Installed only for a verified new installation event.
Ordinary settings Reset to Defaults restores both the search and auto-hide
preferences to On but does not silently unhide songs; Unhide All owns that
operation.

Publish immutable catalog/visibility generations to consumers. Serialize updates
from UI, discovery/install and recovery through one writer. Use atomic replacement
and error reporting: a failed write must not claim a song was hidden or all songs
were restored. Preserve the last valid file on malformed input or migration/write
failure. When enabled, persist auto-hide before initial library results can be
published.

### 4.2 Visibility and package deletion

Keep hidden songs registered with the native song manager. Removing them from DLC
enumeration or metadata would break search, scores, package views and ownership.
Apply one visibility policy when building ordinary lists, all library search
providers (retail and Ultimate), package views and automatic song pools. Filter
before counts, grouping and pagination; handle empty groups and last-item removal.
The remote RhythmVerse catalogue remains a catalogue, not a local library search.

Delete runs asynchronously through a shared package-operation service. Reuse the
current download worker's coordination, diagnostics and invalidation where useful,
with inputs independent of offer tokens. Before deletion, validate the exact
catalogued DLC root against configured content roots, reject root directories,
game/base/Ultimate payloads, traversal and reparse/link escapes, and recheck the
package identity/membership captured by the confirmation. Accept STFS containers
and supported extracted packages only. Never delete an unresolved or built-in
source even if a UI command is forged.

Serialize against installs, scans, restarts and competing mutations. Release or
safely retire previews, menu-music mounts and native file references first. A
locked/active package gets actionable feedback rather than an unsafe removal.
After success or partial removal, invalidate every affected package member in the
store ownership index, DLC cache, guest library and menu-music catalog. Block stale
launches until the existing discovery/cache refresh finishes; retain a native
refresh/restart requirement if live removal cannot be proven safe. The current
store warns that ignoring refresh can crash; shared Manage must close that gap,
not copy an Ignore path into a new route. Partial failures report what happened
and keep the refresh obligation. Never erase scores or claim rollback of files
already removed.

### 4.3 Load-recovery auto-hide

Extend the existing fatal-load attempt lifecycle with a bounded durable handoff:
attempt id, exact song/source identity, optional safe display title, and outcome.
Publish it **before** entering native loading setup, and clear/disarm it on
successful load, normal cancellation, unload or clean exit as appropriate. All
song-start routes, including Play Current Song and restart-song, must capture the
same identity. The monitor must associate its recovery with this exact attempt,
not merely reuse the most recent song on disk.

The existing external monitor remains the restart authority. On a recovery boot,
resolve **Auto-hide Failed Songs** from the settings service before any hide or
popup selection. Validate and consume the matching one-shot handoff in either
setting state. With On, write an idempotent hide using the shared persistent store
and apply visibility before library/menu-music publication. With Off, skip the
hide entirely and use the current load-guard recovery flow and popup unchanged;
consume the attempt so enabling auto-hide later cannot act on an old crash.
Disabling auto-hide does not unhide songs already hidden manually or by recovery.
No automatic replay. Preserve existing loop prevention, clean-exit behavior and
recovery-boot overrides; those overrides must not force auto-hide On. An ordinary
unrelated startup must not auto-hide from a stale breadcrumb; synthetic crash
tests use isolated state.

On with a successful hide, recovery copy: “The game restarted after a song failed to load.
‘<song>’ was hidden from your library.” Follow with the effective search behavior
and Unhide All location, using the same On/Off wording policy as Hide. Do not
diagnose confirmed corruption from an exit code. Missing/ambiguous identity or a
failed persistence write retains truthful fallback recovery copy and a diagnostic;
it must never hide another song or say Hidden without success. Unhide All clears
automatic hides too; only a new eligible failed attempt may hide that song again.

With auto-hide Off, retain the existing `rbbz_load_crashed` message exactly:
“The game crashed while loading a song and was restarted. Returned to the main
menu. Try another song.” Keep the same native popup timing and dismissal. Do not
append hiding, search or Unhide All instructions to this variant. Retain this
original locale entry and select a separate hidden-song variant only after a
successful enabled hide.

## 5. Implementation sequence and exit gates

### P0 — Trace native routes and freeze contracts

Inspect retail and Ultimate song-select records and providers: selected-song
access, X binding/footer, lists/grouping, search implementations, package-result
construction, native refresh and Back lifecycle. Map song/source identity before
load setup, actual package mounts/open-file lifetimes and store-owned ambiguity.
Investigate existing profile/SongStatus statistics for a reliable play counter;
omit the row if unavailable. Inventory supported package names/identities and
structured/extracted/manual installation paths.

**Deliver:** verified seam/call table, record fixtures, final identity/date schema,
return-context contract and deletion/refresh strategy. Mark runtime assumptions
unproven. No UI-only filter that leaves Ultimate search or stale launches exposed
passes this gate. Recheck current load-recovery and navigator work before edits.

### P1 — Shared catalog, persistence and date observations

Add a focused store-independent module, provisionally under `src/library/`, with
SDK-free identity/state/visibility policy and a runtime catalog adapter. Reuse
package readers and enumeration results. Reconcile dates on cached and uncached
discovery, refresh and verified RhythmVerse install completion. Wire generation
publication and atomic persistent mutations. Keep optional count unavailable
unless P0 proved a source.

**Exit:** exact package membership and per-song hide state survive restart/cache
deletion and do not depend on a store token, network or enabled RhythmVerse.

### P2 — Guest Manage panel, footer and package results

Compose the shared info-style guest screen, fields, four action slots,
confirmations and progress/error states through served records/locales. Add
X Manage to My Songs using the actual action/remapping path; restore existing
footer bindings when leaving. Wire package-result browsing and explicit return
contexts from both library and installed RhythmVerse offers. Replace the store's
Delete entry with Manage, retaining its ownership readiness behavior.

**Exit:** built-in, single-DLC and pack screens show accurate capabilities;
Cancel/Back and modal focus always return to the proper caller. No host overlay.

### P3 — Visibility, settings and safe package operations

Integrate shared visibility into ordinary lists, all native/Ultimate searches,
explicit package views and automatic selection/menu-music pools. Add the native
default-On search and auto-hide preferences and confirmed Unhide All command to
Music Library & Store, using the existing settings schema/service/bridge and
action mechanisms. Cover missing-key defaults, persistence, effective-value
precedence and Reset to Defaults for both preferences; auto-hide gates only P4.
Allocate ids/tokens only after checking the current registry; no arbitrary frozen
id is assigned by this plan. Implement the shared exact-package deletion worker,
mount retirement, failure handling and mandatory refresh barrier from §4.2.

**Exit:** hiding one pack member never hides its siblings; deleting a pack removes
its complete confirmed membership; no route can launch removed stale metadata.
Settings apply at the declared safe boundary without a feature master switch.

### P4 — Failed-load handoff and recovery messaging

Extend `BeginCrashRecoveryAttempt` and monitor/recovered-boot plumbing with exact
identity/attempt handoff. Consume it before initial library publication and gate
the shared-store hide on the effective Auto-hide Failed Songs preference. Select
the new native popup variant only after an enabled hide succeeds; Off keeps the
existing flow and text. Missing identity and write failures retain a truthful
recovery. Off consumes the handoff without mutating hidden-song state.

**Exit:** with On, one eligible fatal load hides exactly its failed song across
restart. With Off, recovery and its message match current load guard and no new
hide is written. Success, clean exit, unrelated crash and stale handoffs hide
nothing in either setting state.

### P5 — Automated verification, build and owner acceptance

Use focused SDK-free tests for identity, state migrations, atomic write failures,
date provenance, visibility contexts, duplicate sources and exact deletion scope.
Extend existing `store_library`, `store_download`, `store_payload`, `menu_payload`,
settings and `song_load_monitor` checks where their contracts change. Record/guest
fixtures must exercise real provider/record seams where feasible; testing a helper
alone cannot prove Ultimate search exclusion or native refresh safety.

Build before requesting runtime testing:

```powershell
cmake --build --preset win-amd64-release
```

Run the new focused targets and affected CTest targets from
`out/build/win-amd64-release`. Before navigating the game/UI, ask whether the owner
wants to perform those tests themselves and wait for the answer, per `AGENTS.md`.
A later autonomous-testing authorization covers its stated scope. Use disposable
test DLC packages for deletion and isolated user-data state for crash injection.

| Acceptance scenario | Required result |
| --- | --- |
| Retail and Ultimate, RhythmVerse disabled/offline | My Songs X Manage works with the shared local service and normal trial/license gates. |
| Built-in song | Title/artist and Built-in Song; Hide works; Delete is disabled; no DLC date or See Package. |
| Manually installed single DLC, no catalogue match | Accurate source and observed date; exact deletion works without a store receipt. |
| Multi-song package with hidden members | Package name/count are accurate; See Package shows every member; Hide affects only one; confirmed Delete removes all package songs. |
| Duplicate title/artist and duplicate guest keys | Correct resolved source; ambiguity requires a chooser; no unrelated package is removed or hidden. |
| Hide with search On | Ordinary lists/counts/groups omit it; retail and Ultimate search can find and intentionally play it; confirmation explains recovery. |
| Hide with search Off, including an existing result set | All library searches omit it immediately at the safe boundary; stale results cannot bypass the policy; confirmation no longer promises search discovery. |
| Hide last song / last group / every song | Stable focus, correct counts and native empty state; no crash or phantom heading. |
| Unhide All, empty/nonempty and after auto-hide | Proper action availability/confirmation; all reasons cleared; songs reappear; scores and installed state remain intact. |
| Store → Manage → Cancel, Hide, See Package/Back | Same originating offer and catalogue context; Manage / Cancel remains while installed. |
| Store → Manage → Delete | Return context survives; live ownership becomes uninstalled and its action changes appropriately. |
| Preview/menu-music active; locked, read-only or partially removed package | Safe retirement or useful failure; no success claim on failure; every removed member requires refresh before play. |
| Cache hit/miss/rebuild, moved library, reinstall | Hides and dates persist by identity; First Seen is never presented as Installed; built-in dates stay absent. |
| Auto-hide preference missing, persisted, changed or reset | Defaults On, persists through normal/recovery boots, honors effective-value precedence and changes without an extra restart; Reset to Defaults restores On without clearing hides. |
| Eligible fatal load with known identity, auto-hide On | Recovery boot persists hide before visibility publication; popup says which song was hidden and describes search On/Off correctly. |
| Eligible fatal load, auto-hide Off | Existing restart, main-menu return, popup text/timing and dismissal remain unchanged; no new hide or hide-related popup claim, regardless of search preference. Existing hides and manual Hide still work. |
| Auto-hide Off during recovery, then enabled later | Consumed attempt never causes a delayed hide. Only a new eligible failure can trigger automatic hiding. |
| Auto-hide On, load-guard recovery disabled | No new monitor/restart behavior or automatic hide; the preference does not enable load guard. |
| Successful load, clean exit, stale handoff, unrelated startup/crash | No accidental hide or repeated recovery action. Missing identity/write failure gets truthful fallback text. |
| Unhide a previously failed song, then load it again | Explicit retry is possible; a new eligible failure can hide it again once only with auto-hide On. Off retains ordinary recovery without rehiding. |
| Controller remapping, keyboard/mouse, repeated X and modal Back | One action per activation, no background play/delete, coherent focus/footer and correct caller return. |

### P6 — Documentation and delivery

Update the owning menu/input/library study, settings menu/contracts/status as
needed, the toggle registry for both new preferences, DLC guidance and known
issues for measured remaining limits. Document persistent-file ownership and
pack-wide deletion. Do not advertise an Installed date or play-count source that
was not proved. Move this plan to in-progress only when implementation begins,
and record automated versus owner/runtime evidence separately before finishing.

## 6. Open tracing items

- **O1 — Guest data:** exact selected-song, search-provider and package-results
  seams on retail and Ultimate; play-count availability and profile semantics.
- **O2 — Identity:** stable identity for STFS and extracted packages, duplicate
  guest keys, moved packages and active content families; resolve before mutation.
- **O3 — Deletion lifecycle:** mounted package retirement and whether native removal
  can refresh safely without restart. A truthful mandatory restart is acceptable
  if required; playable stale entries are not.
- **O4 — Recovery identity:** capture before vulnerable setup for every start
  route, bind it to the monitor's attempt and reject stale/unrelated boot records.
- **O5 — Presentation:** guest widget capacity for fields and warning text, source
  chooser for ambiguous store matches, and localized/remapped X footer width.

These items are implementation gates, not requests to the owner to supply
information already discoverable from the code and installed guest records.
