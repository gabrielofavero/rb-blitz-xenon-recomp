# Startup splash investigation (2026-11-07)

## Expected routes and observed failure

Normal startup should show the Harmonix logo on the retail route and the Ultimate
Mods logo on the Ultimate route, then reach **Press A to Start**. Fast startup skips
the logo playlist on either route. During the attempted two-logo Ultimate sequence,
the user repeatedly saw Harmonix for a long time and then the title prompt; the
Ultimate Mods image never appeared. The same result occurred through `run-game` in
`dev.ps1`.

## Startup records and code path

The retail `ui/startup/gen/startup.dtb` contains the Harmonix scene. The installed
Ultimate record starts with `ulti/ultimate_mods.milo`, followed by conditional
Harmonix and North American ESRB entries, and initially has `wait_for_splash FALSE`.
`BuildMenuRecords` reads the record from the selected game-data route. Previously it
reordered the Ultimate record to Harmonix, Ultimate Mods, then the conditional ESRB
entry, and set the wait flag to `TRUE`. The retail record was left in its authored
order and likewise set to wait. See `SelectUltimateStartupLogo`,
`KeepStartupLogos`, and the `BuildMenuRecords` call in
[`menu_payload.cpp`](../../src/settings/menu_payload.cpp).

The startup scenes are genuine guest scenes. The Ultimate Mods image is present in
its MILO and can render when it occupies the first playlist slot. That observation
rules out a missing or wholly unreadable Ultimate logo asset as the explanation for
the user-visible failure.

## Multi-scene experiments

Earlier trials swapped the order of Harmonix and Ultimate Mods. Whichever logo was
first appeared; the second did not become visible. This followed the playlist slot,
not a particular image. Diagnostic runs also recorded the second scene being loaded,
its mesh being enabled, and a six-index draw being submitted. Those observations
show that reaching a second scene does not guarantee a visible logo.

The first and second logo draw submissions used the same vertex and pixel shaders,
depth and blend settings, color control, scissor, and surface settings. Of the
compared GPU registers, only `CP_RB_WPTR`, `D1GRPH_PRIMARY_SURFACE_ADDRESS`,
`VGT_DMA_BASE`, `RB_COPY_DEST_BASE`, and `SHADER_CONSTANT_FETCH_31_4` differed.
The pixel shader samples texture slot 0, so the changed slot 31 fetch is not direct
evidence of the logo texture changing. The slot 0 fetch descriptor was unchanged,
and the sampled 1 MiB physical-memory snapshot had the same hash at the compared
draws. This points toward a scene or texture handoff problem, but it does not
identify the exact guest or renderer operation at fault.

Both MILO scenes contain identically named logo objects. A trial renamed the
Ultimate scene's matching object names in an override archive; the override was
confirmed present, but the second draw still showed the same memory snapshot.
Name collision alone therefore did not explain the failure. Trials involving
scene-timer replay, GPU clear/depth behavior, material/texture selection, and
first-scene transform state likewise produced no verified two-logo sequence.
Tests across Vulkan and D3D12 did not establish a renderer-specific fix. The
automated desktop capture available in the sandbox was unreliable (foreground
window unavailable and black `PrintWindow` output), so a draw trace is not treated
as visual proof.

## Route-specific result

Normal Ultimate startup reduces its installed playlist to the first, authored
`ulti/ultimate_mods.milo` scene and preserves `wait_for_splash FALSE`. The selection checks
the known installed record shape before editing it; an unexpected shape is rejected
instead of silently choosing another scene. Normal retail startup retains its
authored Harmonix playlist and wait flag. Fast startup still clears the
playlist. This uses the logo known to work in the first slot for each route.

The reason a second logo fails to become visible remains unresolved. The one-logo
routes avoid that handoff while preserving the requested original logo identity.

## Validation of the earlier wait-enabled revision

`cmake --build --preset win-amd64-release` and the `menu_payload` CTest passed.
The payload test runner also passed against both installed source startup records.
Controlled boots of the rebuilt executable reached the title flow with
`--ultimate_mode=1` (provider mounted) and `--ultimate_mode=0` (retail data). The
generated archives from those exact boots were extracted and their startup DTBs
parsed and checked: Ultimate has exactly one scene, `ulti/ultimate_mods.milo`,
retail includes Harmonix and no Ultimate Mods scene, and both wait flags are
`TRUE`. The game windows were hidden for these controlled boots, so the generated
record checks establish the selected scenes but are not a new on-screen capture.

## Recording and follow-up (2026-11-08)

The owner's 12.3-second recording shows the Ultimate Mods logo on one frame near
10.47 seconds, followed by two black frames and the title screen. The matching boot
log shows the first startup-logo GPU activity at 00:05:36.2 and the title screen at
00:05:41.1, almost exactly the authored five-second logo duration. The visible logo
arrives at the end of that period. A plausible cause is the forced
`wait_for_splash TRUE`: the installed Ultimate record authors `FALSE`, and waiting
appears to prevent the guest from presenting the logo during that interval.

The normal boot edit now leaves each route's authored wait flag intact. The record
tests check that Ultimate keeps `FALSE`; on-screen behavior still needs a new boot
capture.

## Four-route recording review (2026-11-08)

The four owner recordings show the same RGB `(1, 0, 72)` full-screen interval on
DirectX. Retail Normal holds it for about 4.5 seconds before showing Harmonix
briefly; Ultimate Normal holds it for about 2.6 seconds before briefly showing
Ultimate Mods. The two Fast recordings show about 0.2 seconds of the color and
no startup logo. The owner reports the same interval is black on Vulkan. This
backend difference and the brief correctly selected logos point to presentation
of an incomplete startup frame, but do not yet establish where the missing
intermediate logo frames are lost. Neither the preboot loading icon nor its new
handoff introduced the behavior; the owner observed it before that feature.
The continued Ultimate failure with its authored `wait_for_splash FALSE` rules
out the earlier wait-flag hypothesis as a complete explanation.

The autosave warning mismatch has a separate, record-level cause. Retail
`ui/gen/ui.dtb` sets `first_screen` to `autosave_warning_screen`, while Ultimate
sets it to `splash_screen`. Clearing the startup logo playlist for Fast did not
change that assignment. `BuildMenuRecords` now selects the title's authored
warning screen for both Normal routes and `splash_screen` for both Fast routes.
It checks the installed assignment shape before editing. The headless payload
test and parse/recompile checks against both installed UI records pass. Visible
behavior of the rebuilt executable still needs confirmation in the game.

## Short-boot follow-up (2026-11-08)

The owner reported that a settings-change restart with Normal selected still ran
the Normal first-screen route. The restart handoff already auto-started the local
user and skipped offline prompts, but `InstallMenuPayload` decided whether to
clear the logo playlist from the saved Fast preference alone. It now uses the
settings-restart handoff as a one-boot Fast override. The saved preference does
not change.

Retail Fast also displayed the guest's “You have unlocked the full game” popup
without any input. The exact locale token is `error_signin_unlocked_full_game`.
Recent retail logs show `license_mask = 1` before guest launch on both Normal and
Fast. Fast reaches the splash screen about five seconds earlier because it skips
the authored autosave prelude. The first attempted guard on
`error_popup_panel.show_popup` did not suppress the message. The underlying
guest notification timing is still unidentified.

The owner also confirmed that the blank interval remains blue on DirectX and
black on Vulkan. This build does not alter renderer presentation or the preboot
loading icon. The logs show only a few logo draws over the logo's multi-second
duration, but do not establish whether the stall is in the guest splash worker,
the UI thread, or presentation.

### Retail Fast popup retest

The owner retested the purchased Retail Fast route and still saw the full-game
message. The run's generated `ui/error/gen/error_general.dtb` was extracted from
the mounted archive and contains the exact-token guard, so a record-assembly
failure does not explain the result. A diagnostic revision recorded no calls to
`error_popup_panel.show_popup` during another owner-confirmed occurrence. The
message appears to use the full-screen `ui/net/gen/error_flow.dtb` route: its
`error_screen` reads the native `error_token` property on entry. The ineffective
popup edit was removed. On purchased short boots, the generated error-screen
entry now checks that one token, clears the error, and redirects to the title's
normal discovery screen. Its method entry and token match are logged separately
for the next owner test. Other error tokens retain the original screen entry.
The on-screen result remains unverified.

The next owner run confirmed both error-screen markers fired on the exact
`error_signin_unlocked_full_game` token. The redirect reached the menu, but the
error graphic flashed for a fraction of a second. The error panel is now hidden
at its guest `load` method and revealed in `error_screen.enter` only for other
error tokens. The rebuilt record parses against the installed retail source and
the payload tests pass; the flash-free visual result needs a new owner run.

### Frame-by-frame retail Fast recording

The owner confirmed the unlock graphic is gone in the rebuilt retail Fast run.
The 248-frame, 30 fps recording was decoded at every frame. Frames 130–133
(4.333–4.433 s) are the solid blue guest output; frame 134 is black; frames
135–140 show the title's Press Start scene (frame 135 is only half drawn);
frames 141–151 are black; frame 152 begins the game's library loading screen.
The black interval is 11 captured frames, about 367 ms. The usable main menu
appears later, after the native content and graphics preparation screens. This
identifies the brief menu flicker as a real title-scene to black to loading
transition, separate from the earlier blue clear. The boot log records a first
graphics-loading attempt, the automatic unlock error entering, and a second
attempt after the bypass redirects to `server_connect_screen`; that sequence is
consistent with the extra transition, although the log does not prove which
operation clears the black frames.

The short-boot `splash_panel.poll` action previously set the participating user
before calling `ui goto_screen server_connect_screen`. The game's own
`splash_panel.SELECT_MSG` Start-button branch uses the reverse order. The short
boot now follows that authored order, with the one-shot `starting` marker before
the transition. The retail source record still parses, the full build and
payload tests pass, and the visual effect needs an owner retest.

The owner retest still flickered. Its 256-frame, 30 fps recording has title
frames 151–156, then twelve black frames (157–168, 400 ms) before the library
loading screen at frame 169. The log still records an automatic unlock error
entry and two graphics-loading attempts, so changing the Start action order
did not remove that transition. The sampled DirectX run showed black rather
than blue before the title; the earlier run's blue was four frames long.

The error screen's authored panel list contains only `shell_music_panel` and
`error_panel`. Hiding `error_panel` for the automatic unlock token therefore
leaves a fully black screen until the redirect to `server_connect_screen`.
The purchased short-boot record now adds the title's own `splash_panel` behind
the hidden panel. The native error panel is opaque for other tokens, so their
display path remains intact. This should preserve the title image through the
automatic error transition; its on-screen effect remains to be checked.

The owner confirmed this guest-panel change removed the black title flicker.
The following 269-frame, 30 fps Retail Fast recording still shows three blue
frames (151–153, 5.033–5.100 s), one black frame (154), and a half-drawn title
frame (155) before the title is complete. The corresponding log marks
`splash_screen/splash_panel/stable` at 02:12:39.140 and the boot icon handoff at
02:12:39.151. This proves UI state readiness is earlier than complete visible
title pixels; it does not yet identify why the first guest output is blue on
DirectX and black on Vulkan.

The retail and Ultimate `ui/gen/ui.dtb` init records load many panels before
entering the selected first screen, but do not explicitly load `splash_panel`.
For Fast boots, `SetFirstBootScreen` now inserts the native `splash_panel load`
just after `app_label_register`, before the first screen transition. Both
installed source records parse and compile with this edit; the full build and
payload tests pass. The owner's next 220-frame, 30 fps Retail Fast recording
still has one solid blue frame at 5.333 s, followed by five black frames and
then the complete title at 5.533 s. The loading icon is visible over the blue
frame, so the guest/presenter generated the color before the icon handoff.
This was shorter than the three blue frames in the preceding recording, but
preloading alone did not remove the blank interval.

### Startup shader compilation

The SDK enables `async_shader_compilation` by default. When a D3D12 pipeline is
not yet available, its command processor returns before issuing that guest draw;
Vulkan likewise skips a draw while its pipeline is a placeholder. The swap can
therefore present a cleared frame. The D3D12 clear in these captures is RGB
`(1, 0, 72)`; Vulkan presents black. This explains the backend color difference
without treating the host loading icon as the source.

The owner ran the existing build with `--async_shader_compilation=false` on all
four routes and reported that the blank interval disappeared in each. Retail
Fast, Ultimate Normal, and Ultimate Fast used the usual 2x render scale; the
first Retail Normal diagnostic was in safe graphics at 1x after a failed
launch. The result strongly supports missing startup pipelines as the cause,
although it does not prove every skipped draw was counted in a GPU trace.

`SettingsBoot` now temporarily makes shader compilation synchronous before
graphics setup. At the first usable menu after content discovery, it restores
the prior `true` value so later shader and pipeline creation can run in the
background. An explicit `false` value is left alone. The owner then ran the
rebuilt Retail Normal route without an async override and confirmed a clean
Harmonix, autosave, and menu sequence. That run used the usual 2x render scale.
`rb_blitz_686.log` records synchronous compilation before graphics setup and
restoration at the usable menu after content discovery. The other three routes
were verified with the explicit `false` diagnostic flag; their automatic
switch remains inferred from this shared boot path.

### Two-scene Ultimate retry

With synchronous startup pipeline creation now verified on Retail Normal, the
owner asked to retry Harmonix followed by Ultimate Mods on Normal Ultimate.
The installed Ultimate record already has both MILO scenes. The Normal route
selects those two scenes in that order, keeps their authored durations, and
drops the regional ESRB branch. `first_screen` remains
`autosave_warning_screen`, so the intended sequence is Harmonix, Ultimate Mods,
autosave warning, then title/menu. The owner saw only Harmonix and autosave.
The next run's splasher trace showed both different scene objects loading, but
the guest retired Ultimate Mods immediately after loading it. The native state-7
readiness path drains all remaining scenes while its wait flag is false. The
two-scene Ultimate route now sets `wait_for_splash TRUE` to keep the second scene
active for its authored duration. The owner's next check still saw Harmonix for
both five-second slots. The installed Ultimate MILO is a modified Harmonix scene
with the same `HMX_logo.tex` name, and the second draw used the first image.
Replacing the second playlist slot with the distinct ESRB scene showed the ESRB
rating image correctly. That confirms a scene with a separate texture identity
can display after Harmonix. The Normal Ultimate route now copies only the
Ultimate scene's bitmap bytes into the ESRB scene, keeping `esrb_keep.tex`, its
material, and its mesh. The owner verified the visible sequence: Harmonix,
Ultimate Mods, autosave warning, then menu.
