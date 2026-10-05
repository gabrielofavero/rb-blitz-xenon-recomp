# The named UI states the observation harness drives to, and the route to each.
#
# Dot-sourced by scripts/observe_ui.ps1 (and by anything else that wants the same
# names). Every needle and every route here is *measured*, not guessed:
#
#   * the routes are the ones scripts/acceptance_screens.ps1 already walks, which
#     in turn relies on two measured properties of the guest's menus - a list
#     clamps at both ends, and it resets its highlight to the first row whenever
#     it is entered - so a route is "clamp to the first row, tap down N, accept",
#     and the first row of the main menu is PLAY (A on it opens the song list);
#   * the needles are the strings those screens actually draw, as read by
#     Windows OCR on this build (docs/engine/observing.md says how to add one);
#   * the settle times are deliberately longer than the route's own waits: the
#     frame is only comparable once the screen has stopped animating, and the
#     noise floor between two runs of the same build falls from 3.5-5.9 % just
#     after a screen is entered to ~0.14 % once it has settled
#     (docs/backlog.md, "Measure the noise floor").
#
# A state with an empty needle is a state whose text has not been pinned down
# yet; the harness still captures it (and says the expectation is unknown) rather
# than inventing a string that would pass for the wrong reason.

# A list clamps at both ends, so eight taps up is the first row whatever the
# highlight was - the same clamp scripts/acceptance_screens.ps1 uses.
$script:UiClampUp = @()
for ($i = 0; $i -lt 8; $i++) { $script:UiClampUp += "key:lstick_up" }

# Title -> sign-in dialog -> offline prompt -> main menu. Three accepts, because
# the title raises two dialogs before it draws its menu (measured, and what
# scripts/acceptance_screens.ps1's Wait-ForMenu drives when a B left the menu).
$script:UiTitleToMenu = @("key:a", "wait:6", "key:a", "wait:6", "key:a", "wait:5")

# How long the window takes to appear and the title screen to be drawn.
$script:UiBootToTitle = @("wait:28")

# The route to a main-menu row: from the menu, clamp, then tap down `Downs` times
# and accept. Row 0 is PLAY, 1 LEADERBOARDS, 2 ACHIEVEMENTS, 3 HELP & OPTIONS,
# 4 DOWNLOAD CONTENT, 5 EXIT GAME (measured: acceptance_screens.ps1's Downs).
function New-UiMenuRoute([int]$Downs) {
    $route = @($script:UiClampUp)
    for ($d = 0; $d -lt $Downs; $d++) { $route += "key:lstick_down" }
    $route += "key:a"
    $route += "wait:4"
    return $route
}

# HELP & OPTIONS is itself a list of four pages, one row down each.
function New-UiHelpRoute([int]$Downs) {
    $route = @($script:UiBootToTitle) + @($script:UiTitleToMenu) + @(New-UiMenuRoute 3)
    $route += @($script:UiClampUp)
    for ($d = 0; $d -lt $Downs; $d++) { $route += "key:lstick_down" }
    $route += @("key:a", "wait:4")
    return $route
}

function New-UiMenuState([int]$Downs) {
    return @($script:UiBootToTitle) + @($script:UiTitleToMenu) + @(New-UiMenuRoute $Downs)
}

$script:UiStateOrder = @(
    "boot", "title", "main-menu", "song-list", "in-song", "pause",
    "help-options", "controls", "calibration", "audio-video", "credits",
    "leaderboards", "download", "exit-confirm"
)

$script:UiStates = @{}

$script:UiStates["boot"] = @{
    Needle = ""; Settle = 3; Note = "the window, before the splash has finished"
    Route  = @("wait:6")
    # Measured 2026-10-04: the second run's capture was 85 % different and its mean
    # luminance was 4.4 against the first run's 75.2 - the frame was simply black,
    # because the window exists (Wait-ForWindow returns on the handle) before the
    # guest has drawn anything. This is a race with the splash, not a screen.
    Settled = $false
}
$script:UiStates["title"] = @{
    Needle = "TO START"; Settle = 6; Note = "the title screen; 'TO START' is its own prompt"
    Route  = $script:UiBootToTitle
    # Measured 2026-10-04 on five same-build pairs of this state: 0.046 %, 1.978 %,
    # 18.28 %, 10.74 % and 1.894 % of the frame - the last three captures all OCR'd
    # as 'TO START', so they are the same screen. What moves is the aurora behind it,
    # whose phase two runs need not share and no settle time removes; a pair is a
    # draw from that distribution, which runs from under 0.05 % to nearly 19 %. The
    # 2.5 % ceiling the two low draws once suggested was not a floor: it failed a run
    # of the same build. OCR of 'TO START' is the control instead.
    Settled = $false
}
$script:UiStates["main-menu"] = @{
    Needle = "DOWNLOAD CONTENT"; Settle = 6; Note = "the main menu's own row names"
    Route  = @($script:UiBootToTitle) + @($script:UiTitleToMenu)
    # The main menu draws the same animated background as the title screen and has
    # the same measured problem (see the title entry): a same-build pair whose
    # capture OCR'd as this menu differed by 17.71 % of the frame.
    Settled = $false
}
$script:UiStates["song-list"] = @{
    Needle = "YOUR SONGS"; Settle = 6; Note = "A on row 0 (PLAY); the list previews rows"
    Route  = (New-UiMenuState 0) + @("key:a", "wait:5")
    # Measured 2026-10-04: both runs of this route captured the screen's one-time
    # entry tutorial ('IN ROCK BAND BLITZ ... PRESS O TO BEGIN') instead of the
    # list, so the needle read on neither and the 0.473 % pair difference is two
    # readings of that overlay, not of the song list. Left as written rather than
    # re-pointed at the overlay, which is what the save's history decides, not what
    # the screen always draws - open in observing.md.
}
$script:UiStates["in-song"] = @{
    Needle = ""; Settle = 10; Note = "A on the list's first row (Random Song). Starting a song can write to the save; see observing.md"
    Route  = (New-UiMenuState 0) + @("key:a", "wait:5", "key:a", "wait:10")
    # Two same-build runs differ by 72.78 %, and by construction they need not be
    # the same content: the route accepts the list's first row, 'Random Song'. A
    # frame that is not the same song twice cannot be diffed.
    Settled = $false
}
$script:UiStates["pause"] = @{
    Needle = ""; Settle = 4; Note = "START during a song"
    Route  = (New-UiMenuState 0) + @("key:a", "wait:5", "key:a", "wait:10", "key:start", "wait:3")
    # 56.03 % between two same-build runs whose OCR both read 'GAME PAUSED', so the
    # menu was right both times and what differs is the live scene behind it.
    Settled = $false
}
$script:UiStates["help-options"] = @{
    Needle = "HOW TO PLAY"; Settle = 6; Note = "HELP & OPTIONS (main-menu row 3); the page is titled How to Play"
    Route  = New-UiHelpRoute 0
    # Measured 2026-10-04: the full-frame OCR of this screen loses the title (it is
    # stylised, and 3840x2160 cannot be upscaled - Windows OCR refuses anything
    # past its size limit), while a crop of the title band reads "How to PLAY".
    # The crop is in the captured frame's pixels, so it follows the capture size.
    Crop   = "150,100,2700,500"
}
$script:UiStates["controls"] = @{
    Needle = "CONTROLLER"; Settle = 6; Note = "How to Play -> Controls"
    Route  = New-UiHelpRoute 1
    # 5 % between two same-build runs that both OCR'd 'CONTROLLER', all of it in the
    # page's top third (bands 5 / 28 / 17 %, the rest pixel-identical) - above the
    # 1 % default, so the ceiling would fail a run of the same build.
    Settled = $false
}
$script:UiStates["calibration"] = @{
    Needle = "CALIBRATION WIZARD"; Settle = 6; Note = "How to Play -> Calibration"
    Route  = New-UiHelpRoute 2
    # 5 % between two same-build runs that both OCR'd 'CALIBRATION WIZARD'.
    Settled = $false
}
$script:UiStates["audio-video"] = @{
    Needle = "OVERSCAN"; Settle = 6; Note = "How to Play -> Audio/Video"
    Route  = New-UiHelpRoute 3
    # 13.1 % in the measured pair, and the capture was the help index rather than
    # this page (its OCR read 'HOW TO PLAY CONTROLS CALIBRATION AUDIO/VIDEO
    # CREDITS'), so the number is a route miss as much as a difference.
    Settled = $false
}
$script:UiStates["credits"] = @{
    Needle = "PROGRAMMER"; Settle = 8; Note = "How to Play -> Credits; the page scrolls in, so it settles longer"
    Route  = New-UiHelpRoute 4
    # 75.38 %, and the capture was the Audio/Video page rather than this one (its
    # OCR read 'AUDIO/VIDEO ... OVERSCAN'), so the page scrolls in behind a route
    # that landed a row short. 8 s of settle did not reach a frame two runs share.
    Settled = $false
}
$script:UiStates["leaderboards"] = @{
    Needle = "CAREER LEADERBOARD"; Settle = 6; Note = "main-menu row 1; empty offline, and names Rock Central as the reason"
    Route  = New-UiMenuState 1
    # 10.535 % between two same-build runs that both OCR'd 'CAREER LEADERBOARD'.
    Settled = $false
}
$script:UiStates["download"] = @{
    Needle = "ESTORE"; Settle = 6; Note = "main-menu row 4; the eStore notice"
    Route  = New-UiMenuState 4
    # 14.62 % between two same-build runs that both OCR'd the eStore notice, the
    # same animated background as the title screen behind it.
    Settled = $false
}
$script:UiStates["exit-confirm"] = @{
    Needle = "WANT TO EXIT"; Settle = 6; Note = "main-menu row 5; the title's own wording, 'ARE YOU WANT TO EXIT THE GAME?'"
    Route  = New-UiMenuState 5
}

# The routes above assume a fresh boot, because that is what makes a capture
# comparable: the same number of menu presses from the same starting screen.
#
# MaxDiff is the per-state pixel-difference ceiling, in percent, for -Compare; 0
# means "use the harness default". It is honoured only where the state is settled
# (the default). On a state marked Settled = $false the ceiling is reported and not
# applied, because the measurements above say the same build can differ by more
# than any useful ceiling - and a ceiling that fails a run of the same build is
# worse than no ceiling, since it is how a real change gets excused later.
#
# `Settled = $false` is the measured verdict that this screen has no frame two runs
# share: the OCR needle gates the run and the diff corroborates it. Measured
# 2026-10-04 at -CompareScale 4 (observing.md carries the whole table):
# help-options, song-list and exit-confirm reproduced at or below the 1 % default
# and keep the diff as a gate; every other state measured above it, or drifted to a
# different screen entirely.
