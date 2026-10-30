# Default-target offline powerups — implementation plan

**Status:** Planning only, 2026-10-26. No implementation, build or game/UI testing in
this pass. Static inspection of the installed retail and Ultimate records informs
the requirements; native lock, pricing and transaction seams remain P0 work.

## 1. Request and scope

Restore useful offline powerup play on the full **Rock Band Blitz** target
(`common`), independently authored from retail behavior. Ultimate keeps its own
working implementation. The owner's follow-up explicitly requires disabling the
powerup cost system as well.

| Id | Requirement |
| --- | --- |
| R1 | A full retail installation must support offline powerup selection, equipping and actual use without installing Ultimate. |
| R2 | One native, default-**On** boolean: **Help & Options → General → Achievements & Unlocks → Unlock Powerups: On / Off**. Its help text must explain that it enables all normal powerups offline **for free**. |
| R3 | On means the complete feature: offline access, all normal selectable powerups unlocked, and no powerup cost. Zero coins/cred must never prevent selection, retainment of equipment, song launch or use. |
| R4 | Disable powerup prices, affordability checks, deductions and remote purchase transactions on the active restoration path. Show free/zero cost truthfully through the guest's widgets; do not inflate currency balances or simulate a successful server purchase. |
| R5 | Off restores the original retail offline route and restrictions after the documented application boundary. It must not leave permanent unlocks or fabricated balances behind. |
| R6 | Execute only on the full default target, while offline. The native row is absent on Ultimate and Demo; an installed but inactive Ultimate payload must not disable restoration on `common`. Demo/trial restrictions are preserved. |
| R7 | Preserve Ultimate's existing default, `Unlock All` row, persistence and behavior. No reuse of its scripts, macros, serialized records or broad unlock operation. |
| R8 | Cover normal song selection, Play Current Song, restart/change-powerup routes and returns from gameplay. Tutorials and deliberately scripted loadouts retain their authored behavior. |
| R9 | Keep the feature powerup-specific: no song/plot unlocks, achievement grants, fake online state or unrelated economy changes. Use guest screens and widgets, with no new host overlay. |

**Product decision:** one switch owns the three parts in R3. There is no second
cost or offline-access setting in this scope. The game still offers its normal
“none” selections. “All” means the retail selectable powerup inventory; debug,
unused and tutorial-only entries are not promoted into the menu. Determine in P0
whether retail upgrade entries participate in that inventory and document their
normal effect before applying the policy to them.

## 2. Findings from the current tree

Inspected 2026-10-26. Newly extracted, gitignored evidence is under
`out/_swap/offline-powerups-plan/`: `retail-song_select.tree.txt`,
`retail-powerup_select.tree.txt`, `retail-powerups.tree.txt`,
`ultimate-song_select.tree.txt`, `ultimate-ulti_init.tree.txt`, and the
`ultimate-records/` record tree. These are static observations, not runtime proof.

| Evidence | Finding and implication |
| --- | --- |
| Retail `ui/song_select/gen/song_select.dtb`, `song_chosen` | The existing gate is `rock_central is_login_blocked OR user_mgr is_participating_user_online`. Its other branch writes `fake_powerups` into the loading screen and goes directly to loading. Restoring only lock checks cannot expose the normal offline selection flow. |
| Installed Ultimate `ulti/gen/ulti_init.dtb` | `unlockall` is initialized to `TRUE`. |
| Installed Ultimate `ui/song_select/gen/song_select.dtb`, `song_chosen` | `unlockall` adds a route to `powerup_select_screen` ahead of the retail gate. Offline access is therefore coupled to that switch in this installed payload. |
| Installed Ultimate `ui/splash/gen/splash.dtb` | When `unlockall` and offline, the participating user receives `set_unlock_all`. This is a broad operation; it is unsuitable for our powerup-only implementation. Its complete native effects have not been traced here. |
| Installed Ultimate `ui/pause/gen/pause.dtb`, `restart_song` | The same switch changes the restart route, with a tutorial exception. Initial selection alone is insufficient coverage. |
| Retail and installed Ultimate `ui/powerup_select/gen/powerup_select.dtb` | Both still contain native calls including `remove_powerup_if_too_expensive`, `get_cost`, `update_ability_to_afford`, `purchase_powerups`, and `purchase_complete` handling. Reaching the screen or changing a displayed price does not by itself establish free functional play. How the installed mod makes these calls work is **not established** by the script inspection. |
| Retail `config/gen/powerups.dtb` | Entries carry `cred_unlock` thresholds and types such as `active`; there is a separate `upgrades` list. Build policy from the installed retail inventory rather than a copied mod list. |
| [schema.cpp](../../../src/settings/schema.cpp), [settings_screen.cpp](../../../src/ui/settings_screen.cpp) | `kUnlockAll` is an Ultimate provider row, default true. The guest Achievements & Unlocks page already has native and provider rows. Add a separate native setting there. |
| [ultimate_adapter.cpp](../../../src/settings/ultimate_adapter.cpp), [settings_payload.cpp](../../../src/hooks/settings_payload.cpp) | Existing `unlockall` has a native persistence exception under `[ultimate_overrides]` and is seeded into the provider's guest state. Do not repurpose this binding. |
| [settings_screen.cpp](../../../src/ui/settings_screen.cpp), `PlayCurrentSongBody` | Play Current Song already invokes `song_select_screen song_chosen` after resolving/mounting the audible song. Restoring the shared retail handler should cover this entry without a second direct-navigation implementation. |
| [local_save.cpp](../../../src/hooks/local_save.cpp) | Local progress is restored through the native SaveData serializer, including after guest teardown. Verify that restoration policy neither contaminates this save nor disappears after restoration. |

The owner's recollection of two settings is not supported by the inspected
payload: its `Unlock All` switch controls both offline routing and the broad user
unlock call. The independent implementation uses the narrower label requested
here and explicitly includes free use. Ultimate's existing UI remains as it is.

## 3. Decisions

| Id | Decision |
| --- | --- |
| D1 | Allocate a new native General id, provisionally `SettingId::kUnlockPowerups = 10` (free in the inspected tree; recheck before implementation). Token `general.unlock_powerups`, storage `[general] unlock_powerups`, boolean default true. Keep id 5 / `general.unlock_all` and `[ultimate_overrides]` separate. |
| D2 | Carrier: `enhancements_unlock_powerups`, default true, registered with restart lifecycle and mapped through the existing native cvar bridge. Preserve config/environment/command-line precedence, effective-value reporting and command-line locks. No migration from Ultimate's `unlockall`: the two settings remain independent. |
| D3 | Initially use `ApplicationStrategy::kRestart`. The save/restart UI must accurately report pending changes; no claim of hot application without measured support for refreshing loaded records, lock state and equipment safely. |
| D4 | Compute one boot eligibility fact from the **resolved effective running target**, full-license state and mounted content mode. Native restoration requires `common` with no active Ultimate content; execution also checks the actual offline state. Neither `!provider.Active()` alone nor payload-directory presence is a valid target test. Honor explicit target/license overrides, safe-mode policy and contradictory merged-content cases by declining uncertain activation and logging why. A pending target change does not affect the running process. |
| D5 | Row visibility uses target eligibility independently of the setting's value and connectivity: the row remains available on an eligible common boot when Off or temporarily online. Hide it on Ultimate/Demo. Keep the schema, bridge descriptor and generated guest page in agreement, including pause variants. |
| D6 | Use an independently authored retail record transform for the shared song-selection and applicable restart/change-powerup routes. Preserve the original handler as the fallback, retain normal mounting/loading/back behavior, and retain tutorial/scripted exceptions. Do not set global online/login/server state to force access. |
| D7 | Implement narrow native policies for powerup unlock eligibility, effective price, affordability/equipment retention and offline selection completion. Prefer the native equipment transfer and next-screen continuation after tracing it; bypass the remote purchase operation before it queues work. Do not invent a transaction ticket or dispatch a forged server success callback. Clear transaction-dependent state according to the proven native offline lifecycle. |
| D8 | On the active path, effective prices and totals are zero, affordability succeeds for valid normal powerups, and equipment is not removed for insufficient funds. Guest cost labels show **Free** or **0** consistently; stale “not enough coins/cred” feedback must not appear. Unlock eligibility and currency are different policies: bypass powerup cred gates without granting cred. |
| D9 | Keep unlock/free-use policy ephemeral. Do not call the broad `set_unlock_all`, rewrite progress or currency, or permanently clear thresholds in the player's save. Preserve normal score/star saving and achievement evaluation; this feature itself grants no achievements. Validate any persisted loadout through retail rules after disabling the feature. |
| D10 | Ultimate boots take the original provider path for every new hook and receive none of our powerup record edits or state writes. The same applies to trial and online execution. Hash/compare served powerup-related records to establish this, rather than relying only on a hidden row. |
| D11 | Record transforms must shape-check the expected retail handlers, be idempotent and preserve neighboring definitions. Unsupported shapes or unproven native layouts decline the restoration as a whole with a specific diagnostic, avoiding a partially free or server-dependent flow. Reuse existing record/DTB infrastructure and compatibility checks. |

## 4. Implementation sequence

### P0 — Trace the complete retail flow

From the installed retail archive and recompiled functions, map the handlers for
`is_locked`, `get_unlock_cred`, `get_cost`, money/cred access, total cost,
`remove_powerup_if_too_expensive`, `purchase_powerups` and equipment transfer to
`loading_screen`. Find any later gameplay or result-submission check that still
depends on an online transaction. Map replay/restart and change-powerup routes,
including tutorials and trial restrictions. Locate the actual offline predicate.

Deliver a call/ownership table with verified guest addresses, inputs/outputs,
which calls mutate state, which enqueue Rock Central work, which data is saved,
and which seams are shared with unrelated systems. Determine the selectable
inventory and upgrade behavior from retail data. Inspect initialization and
SaveData reload ordering so the feature survives a new user/session safely.

**Exit gate:** identify a local equip-and-continue path that works with zero
coins/cred and sends no purchase request. If one does not already exist, define
the smallest independent continuation using native setters and normal screen
transitions. A UI-only unlock or a forged purchase result does not pass P0.

### P1 — Native setting and shared eligibility

Add the id/token/schema/storage/cvar mapping, restart classification, native row
and caption. Suggested caption: “Unlock all powerups for free in offline play.
Requires restart.” Place it immediately after Hide Achievements and before the
provider rows, which keep their own gates.

Wire one resolved boot eligibility fact into settings capabilities and guest
menu configuration; make runtime policy consume that same fact. Use
`CapabilitySnapshot::active_target` with full-license/content facts rather than
the requested value awaiting restart. Use existing settings store behavior for
missing keys, Reset to Default, persistence, read-only failures and overrides.

**Files:** `src/settings/model.{h,cpp}`, `schema.cpp`, `cvars.cpp`, the appropriate
boot/capability wiring, `src/enhancements.cpp`, `src/hooks/settings_payload.cpp`,
and `src/ui/settings_screen.{h,cpp}`. Add only the fields the policy needs.

### P2 — Offline access, unlocks and free completion

Add a focused feature module, provisionally `src/hooks/offline_powerups.{h,cpp}`,
with an SDK-free policy helper for target/offline/setting decisions. Hook only the
native seams established by P0; every inactive path delegates unchanged to its
original. Integrate owned retail transforms with
`src/settings/menu_payload.{h,cpp}` / the existing record build path, and add the
new sources to `CMakeLists.txt`.

Restore entry through the shared `song_chosen` route and applicable pause routes.
Apply the powerup-only lock policy, zero-price/total policy and affordability
policy consistently to providers, descriptions, selection and equipment cleanup.
Finish offline selection through native equipment handoff and loading, bypassing
purchase queueing and any transaction-dependent wait. Keep the normal powerup
engine responsible for effects, scoring, cooldowns and energy consumption.

Treat these as one delivery: the feature is incomplete until a zero-balance
player can select every supported powerup, launch, use it and finish a song.
Reset per-song transaction/equipment state where P0 proves necessary; preserve
equipment across intended restarts without leaking it into tutorials or an Off
session. Log activation/inactivation reason and bounded diagnostics for declined
shapes or unexpected transaction activity.

### P3 — Focused automated verification and build

Add meaningful coverage for the policy truth table (common/Ultimate/Demo,
installed/active payload, online/offline, On/Off, full/trial license and override
conflicts); default/persistence/reset/override handling; and generated main-menu
and pause settings rows. Extend existing settings/store/service and menu-payload
tests instead of duplicating their frameworks.

Record tests must verify intended route edits, tutorial preservation, structural
round trips, idempotence, refusal on unsupported shapes, and no feature edits to
Ultimate/Demo/Off records. Policy/transaction tests must check zero balances,
all supported categories and none selections, consistent zero totals, no
deduction or purchase enqueue, one local completion, and exact equipment handoff.
Cover real native integration where available; helper tests alone do not prove
that gameplay receives the loadout. Include save/reload regression coverage
showing no fabricated currency or permanent unlocks.

Build with `cmake --build --preset win-amd64-release`. Run the new focused test
target plus affected `settings_*`, `menu_payload`, `ultimate_adapter`,
`ultimate_adapter_payload` (when installed), `menu_options` and `local_save`
checks. Use the configured release build directory for CTest. No SDK changes are
expected; justify any discovered need before expanding that scope.

### P4 — Runtime acceptance and documentation

After a successful current build, ask whether the owner wants to perform the
runtime tests themselves, and wait before navigating the game/UI. Independent
inspection, builds and non-UI tests can proceed. A later explicit autonomous
testing instruction authorizes driving the UI within its stated scope.

| Scenario | Required result |
| --- | --- |
| Common, no Ultimate installed, missing preference, offline, zero balances | Row defaults On; library song enters native powerup selection; all supported normal powerups can be selected freely; launch needs no server transaction. |
| Common, Ultimate installed but inactive | Same restoration, without loading/calling Ultimate objects. |
| Active, passive A and passive B loadouts | Selected identities reach loading and gameplay; active effects activate, passive effects apply and scoring responds normally. Finish a song and verify ordinary score/star saving. |
| Costs | Prices/totals show Free or 0; a costly combination remains equipped with zero coins/cred; no insufficient-funds error, deduction, purchase request or transaction wait. Inspect the result path too. |
| Play Current Song | The audible song reaches the same working selection flow with correct mounting, metadata and Back behavior. |
| Restart, change powerups, quit/re-enter, next song, save reload | Correct loadout, no stale transaction or duplicate launch; policy still applies after user/save initialization. |
| On → Off → On, each after required restart | Off restores retail offline behavior with no residual unlock/balance effects; On restores free selection. The preference survives relaunch. |
| Tutorial / scripted loadout | Authored behavior retained; no incorrect powerup screen or equipment injection. |
| Ultimate with its Unlock All On and Off | Existing behavior matches its baseline in both cases; native row absent; no native restoration hooks/record edits activate. Do not silently turn its switch back On. |
| Demo/trial; online common if available | Restoration inactive, original restrictions/transactions retained. If online validation is unavailable, record that limitation explicitly. |

Capture guest UI and loadout/state evidence under
`out/_swap/offline-powerups-acceptance/`, and record every untested leg. Update
the owning menu study, toggle registry, settings plan/status/contracts as needed,
known issues and backlog; move this plan to in-progress when implementation
starts, and to finished only when its required gates are satisfied.

## 5. Open engineering questions

These are P0 investigations, not requests for product decisions:

- Which native checks are powerup-specific and safe to intercept without
  changing unrelated purchases or user unlocks?
- How does the native equipment handoff avoid the remote transaction ticket,
  including on results, replay and save reload?
- Which normal upgrade entries, if any, require a separate unlock predicate?
- Does a persisted selected loadout need explicit validation on an Off boot?
- Where can all required record transforms be validated before any is published,
  so an unsupported content shape cannot activate only half of the feature?

## 6. Evidence reproduction and limits

List local inputs with `python scripts/assets/hmx_ark.py list --hdr
game/gen/main_xbox.hdr -v` and the corresponding
`game/ultimate/gen/patch_xbox.hdr` command. The existing
`scripts/assets/hmx_ark.py extract --hdr <header> --out <scratch-directory>` command
extracts records without modifying the installed archives. Parse the relevant
DTBs using the project's `ui::dtb` reader or the local inspection helper
`out/_swap/dtb_tree.py`; retain fresh tree dumps and input hashes with P0 evidence.
The scratch helper is not a shipped dependency. Do not commit extracted game/mod
records or copy their implementation into the new feature.

This plan establishes the settings contract and delivery gates. It does **not**
claim verified native addresses, offline purchase semantics, an exact selectable
powerup count, or runtime equivalence with Ultimate. Those require P0 and P4.
