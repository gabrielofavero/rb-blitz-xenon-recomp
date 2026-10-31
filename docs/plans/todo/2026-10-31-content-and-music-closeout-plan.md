# Content, store and music close-out

**Status (2026-10-31):** planning only. These are the unresolved content and
playback routes from [known-issues.md](../../reference/known-issues.md), grouped
because they all need a real library or a live menu/song route. Do not treat an
old failure log as proof that the current build still fails.

## Prompt order and execution

| Prompt | Dependency | Result |
| --- | --- | --- |
| C1a | Current build and a representative DLC library | Current verdict on the retail song-list fault |
| C1b | C1a reproduces a fault | Isolated cause, fix and retail-list replay |
| C2 | RhythmVerse install fixture | Same-session A Refresh verdict |
| C3 | Reported toggle combination | Search-header restoration verdict |
| C4 | Reproducible load routes | Menu-music listening and timing evidence |
| C5 | Navigator build and music fixture | Previous-burst listening verdict |

C1a–C1b are the former C1 fault investigation. The other prompts are
independent. Build the latest code before any game/UI acceptance. Before
navigating the game or driving its UI, ask whether the owner wants to perform
that test and wait for the answer. Host checks and code inspection can proceed
without that answer.

### Prompt C1a — Recheck the retail DLC song-list fault

**Starting evidence:** the 2026-10-15 mini-library route faulted on a guest
read of `0x4` when **Your Songs** selected DLC. A bundled song worked and a
copied package faulted too, so symlinks were not the trigger. The current
build has not replayed this control; see [known-issues.md](../../reference/known-issues.md)
and [main-menu.md](../../engine/main-menu.md) §7.2.

**Do:** reproduce with the owner's library and a disposable profile. Compare
the same DLC song through the retail list and *Play Current Song*, plus the
bundled-song control. Preserve the log, content identity and guest fault
address for each route.

**Exit:** either the current build disproves the fault and both records are
corrected, or a repeatable case with a narrow failing boundary hands off to
C1b.

### Prompt C1b — Fix a confirmed retail-list fault

**Trigger:** C1a reproduces the fault.

**Do:** trace the guest path from DLC selection to the fault, correct the
demonstrated boundary, add a focused regression where the seam permits, and
repeat C1a's controls. Keep the retail list and *Play Current Song* results
separate.

**Exit:** the same DLC song plays from **Your Songs** without a guest fault,
and the bundled-song control still works. Update the standing issue with the
fix and measured route.

### Prompt C2 — Verify A Refresh in the same session

**Starting evidence:** the install toast's **A Refresh** enters native
discovery, but [store plan](../finished/2026-10-13-rhythmverse-store-plan.md)
P9-14 never checked the newly installed song on **Your Songs** before restart.

**Do:** install one known offer, choose A on its outcome toast, and check the
native list in that session. Use B followed by next-boot discovery as the
control. If A fails, trace the host rescan → guest enumerator handoff and fix
that boundary.

**Exit:** the new song appears in the same-session list, or a specific
failing boundary is recorded with a follow-up fix prompt.

### Prompt C3 — Verify Search header restoration

**Starting evidence:** the owner captured Recommended overlapping Your Songs
with Recommendations Off and RhythmVerse/Messages On. A mask reapply and
eight-state host check landed, but no game capture established the visual
correction; see [store plan](../finished/2026-10-13-rhythmverse-store-plan.md)
P9-4.

**Do:** reproduce the toggle combination on entry, Search and return. If the
overlap persists, capture the first wrong transition and correct the
guest-authored header state.

**Exit:** the headings stay separated through the three transitions on the
current build, with a capture or equivalent route evidence.

### Prompt C4 — Check menu-music continuity under load

**Starting evidence:** the owner heard breaks during song-cache loading,
RhythmVerse entry and graphics preparation. Buffering, worker priority and
PCM handoff changed; listening and attribution remain open in
[main-menu.md](../../engine/main-menu.md) part 2 §6.

**Do:** repeat those three loads with a no-load control. Record listening
results and time-correlated audio/source/device diagnostics. Attribute any
remaining gap before changing another buffer or worker.

**Exit:** playback is continuous in the tested routes, or the remaining
starvation stage is measured and corrected.

### Prompt C5 — Retest navigator command bursts

**Starting evidence:** bounded burst handling passes host checks; rapid
Previous presses during restart still need the owner's listening retest.
Physical-pad/media-key and title-only pause routes remain unmeasured in the
[navigator record](../finished/2026-10-22-background-music-navigator-plan.md)
§15/§22.

**Do:** run the burst against a known pool, verify the audible return song
and lack of a guest fault, then record which optional hardware routes were
actually available and exercised. Keep untested hardware explicitly
unmeasured.

**Exit:** the burst has a listening verdict and log. A remaining command
failure gets its own narrow defect record.

The old RhythmVerse catalogue-failure note is **closed**: P9-10's October 23
loopback boot measured the popup, retry and recovery. DXT1 album art and the
store's five-pip difficulty are documented limitations, not tasks in this plan.
Native song folders and song management have their own [folder](2026-10-26-native-song-folders-plan.md)
and [management](2026-10-26-in-game-song-management-plan.md) plans.
