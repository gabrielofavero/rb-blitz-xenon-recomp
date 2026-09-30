# Wrong-data acceptance: the runtime half of the game-data fingerprint check.
#
# The check has two halves and they behave differently on purpose.
#
#   * The build half fails closed. tools/fingerprint_check.cpp runs as a CMake
#     gate before codegen and refuses to recompile against a dump other than the
#     one config/game_fingerprints.toml records (exit 1);
#     -DRBBLITZ_ALLOW_MODIFIED_GAME_DATA=ON relaxes it to a warning.
#   * The runtime half warns and continues. RbBlitzApp::LogBootIdentity() logs
#     which revision the running executable was actually booted against and
#     returns. Nothing there aborts, because pointing a built binary at another
#     game/ tree is a supported way to run: an Ultimate payload staged at
#     game/ultimate is one (docs/ultimate-compat.md), a second dump is another.
#
# This script captures that second half from the real boot, for the outcomes it
# has, and takes the build half's verdict on the same file as the contrast that
# makes the design visible:
#
#   retail     the recorded image        gate 0 -> "game data identity: Rock Band
#                                                  Blitz 0.0.0.2 (...)"
#   payload    a valid, different image  gate 1 -> "MODIFIED" + "expected:", and
#                                                  the boot still reaches the title
#   truncated  the image data cut off    gate 1 -> the loader refuses it and says
#                                                  which claim does not fit
#   short      the header itself cut off gate 1 -> the loader refuses it too
#   empty      a zero-length file        gate 1 -> the loader cannot map it
#
# The corrupt cases are the boundary. The identity check lives in
# OnPostLoadXexImage, so it only ever sees an image the XEX loader has already
# accepted: a corrupt copy never reaches it, and no identity line is written for
# one. That is also why the MODIFIED path was once believed to have no live
# capture at all - a truncated copy does not load, and a valid image with a
# different digest was assumed to behave the same way. It does not: the boot
# reports the mismatch and continues to the title screen
# (docs/history/bringup-log.md).
#
# What a corrupt copy does instead is the loader's half (patches/rexglue-sdk/
# 0008-xex-image-bounds-checks.patch): it is refused, and the log says which of
# the header's own claims runs past the end of the file. This script asserts that
# sentence, with the numbers checked against the image it built, for the three
# shapes that reach a different check each: the block table past the end of the
# data (truncated), the header size past the end of the file (short), and a file
# too small to map (empty). A refused image ends with the pre-existing
# 0xC0000374 in the SDK's post-Setup teardown - see docs/known-issues.md - so the
# exit code is recorded, not asserted beyond being non-zero.
#
# The payload image defaults to <GameRoot>\ultimate\default.xex, so that case runs
# only where the community mod is staged and is reported as skipped elsewhere.
# Every case runs vanilla (--ultimate_mode=0): the identity line is what is being
# captured here, and the payload overlay mounting has its own acceptance run
# (docs/ultimate-compat.md section 7).
#
# Usage:
#   .\scripts\acceptance_wrong_data.ps1
#   .\scripts\acceptance_wrong_data.ps1 -GameRoot D:\dump\game
#   .\scripts\acceptance_wrong_data.ps1 -Restore
#
# The script swaps <GameRoot>\default.xex in place - that is the point, the check
# reacts to the image the root actually holds - and puts it back from
# out/m5-wrong-data/default.xex.retail in a `finally`. If a run dies hard enough to
# skip the restore, the next run refuses to start and says so; -Restore then puts
# the image back and exits.
#
# Per case it writes out/m5-wrong-data/<case>.png (the screen it asserted on, for
# the cases that reach one - a refused image never does), <case>.log (a copy of the
# run's log) and <case>-gate.txt (what the build gate said about the same file),
# plus summary.json, then prints a verdict table. Exit code 0 means every executed
# case behaved as recorded.
param(
    [string]$GameRoot,
    [string]$BuildDir = "out/build/win-amd64-release",
    [string]$OutDir = "out/m5-wrong-data",
    # The valid-but-different image. Defaults to the staged Ultimate payload.
    [string]$PayloadImage,
    # Seconds to wait for the window and then for "PRESS ... TO START". Boot is
    # 25-40s here; the OCR wait is what turns a slow boot into a longer wait
    # rather than a false failure.
    [int]$TitleTimeoutSec = 120,
    [int]$CloseWaitSec = 20,
    # Put <GameRoot>\default.xex back from the saved retail copy and exit.
    [switch]$Restore
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$work = Join-Path $root $BuildDir
$exe = Join-Path $work "rb_blitz.exe"
$gateExe = Join-Path $work "rb_blitz_fingerprint.exe"
$logs = Join-Path $work "logs"
$fingerprintFile = Join-Path $root "config/game_fingerprints.toml"
$capDir = Join-Path $root $OutDir
if (-not $GameRoot) { $GameRoot = Join-Path $root "game" }
if (-not $PayloadImage) { $PayloadImage = Join-Path $GameRoot "ultimate/default.xex" }
$imagePath = Join-Path $GameRoot "default.xex"
$retailCopy = Join-Path $capDir "default.xex.retail"
$truncatedImage = Join-Path $capDir "truncated-default.xex"
$shortImage = Join-Path $capDir "short-default.xex"
$emptyImage = Join-Path $capDir "empty-default.xex"
New-Item -ItemType Directory -Force -Path $capDir | Out-Null

if (-not (Test-Path $exe)) { throw "not found: $exe (build $BuildDir first)" }
if (-not (Test-Path $gateExe)) { throw "not found: $gateExe (the fingerprint tool ships with the build)" }
if (-not (Test-Path $imagePath)) { throw "no default.xex under $GameRoot" }

# ------------------------------------------------------------------ setup ---

function Get-Digest([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

# The expected values come from the file the build gate reads and the compiled-in
# expectation is generated from, so this script cannot drift away from them.
$entryBlock = (($(Get-Content -LiteralPath $fingerprintFile -Raw) -split '\[\[files\]\]') |
    Where-Object { $_ -match 'role\s*=\s*"entrypoint"' } | Select-Object -First 1)
if (-not $entryBlock) { throw "no entrypoint entry in $fingerprintFile" }
$expectedSha = [regex]::Match($entryBlock, 'sha256\s*=\s*"([0-9a-fA-F]{64})"').Groups[1].Value.ToLowerInvariant()
$expectedSize = [int][regex]::Match($entryBlock, 'size\s*=\s*(\d+)').Groups[1].Value

if ($Restore) {
    if (-not (Test-Path $retailCopy)) { throw "nothing to restore from: no $retailCopy" }
    Copy-Item -LiteralPath $retailCopy -Destination $imagePath -Force
    $restored = Get-Digest $imagePath
    Write-Host "restored $imagePath (sha256 $restored)"
    if ($restored -ne $expectedSha) { throw "the restored image is not the recorded one ($expectedSha expected)" }
    exit 0
}

# The root has to hold the recorded image before the first swap, or there is no
# image to restore afterwards and a silently modified dump is what would be left.
$imageSize = (Get-Item -LiteralPath $imagePath).Length
$imageDigest = Get-Digest $imagePath
if ($imageSize -eq $expectedSize -and $imageDigest -eq $expectedSha) {
    if (-not (Test-Path $retailCopy) -or (Get-Digest $retailCopy) -ne $expectedSha) {
        Copy-Item -LiteralPath $imagePath -Destination $retailCopy -Force
        Write-Host "saved the retail image to $retailCopy"
    }
} else {
    $canRestore = (Test-Path $retailCopy) -and ((Get-Digest $retailCopy) -eq $expectedSha)
    $advice = if ($canRestore) {
        "Run this script with -Restore to put the saved copy back."
    } else {
        "There is no usable saved copy at $retailCopy; re-copy default.xex from the dump."
    }
    throw "$imagePath is $imageSize bytes, sha256 $imageDigest - that is not the recorded image, so a previous run did not restore it. $advice"
}

# Three fixed corruptions, not random cuts, one per check the loader makes on a
# file it cannot trust. Measured before the loader checked any of them: 1 MiB in,
# the last line the run wrote was the loader's own "Loading XEX image:
# game:\default.xex", and the process died there with 0xC0000005.
$retailBytes = [IO.File]::ReadAllBytes($retailCopy)
[IO.File]::WriteAllBytes($truncatedImage, $retailBytes[0..1048575])
# The XEX header size, read out of the image (big-endian at offset 8) because the
# refusal lines quote it: a hardcoded copy would drift from the dump.
$xexHeaderSize = ($retailBytes[8] * 16777216) + ($retailBytes[9] * 65536) +
                ($retailBytes[10] * 256) + $retailBytes[11]
[IO.File]::WriteAllBytes($shortImage, $retailBytes[0..2047])
[IO.File]::WriteAllBytes($emptyImage, (New-Object byte[] 0))
Write-Host ("retail image: {0} bytes, sha256 {1}, {2}-byte header" -f $expectedSize, $expectedSha, $xexHeaderSize)
foreach ($image in @($truncatedImage, $shortImage, $emptyImage)) {
    Write-Host ("corrupt image: {0} -> {1} bytes, sha256 {2}" -f `
        (Split-Path -Leaf $image), (Get-Item $image).Length, (Get-Digest $image))
}
Write-Host ""

# ---------------------------------------------------------------- helpers ---

# Every helper that touches a .ps1 file has to go through a child PowerShell:
# this machine's execution policy is Restricted (docs/build-and-run.md section 0).
function Invoke-ChildScript([string]$Script, [hashtable]$Named) {
    $argv = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $PSScriptRoot $Script))
    foreach ($k in $Named.Keys) { $argv += @("-$k", [string]$Named[$k]) }
    $out = & powershell @argv 2>&1
    if ($LASTEXITCODE -ne 0) { throw "$Script failed: $out" }
    return $out
}

function Capture-To([string]$Path) {
    # Refuses to save a shot of the wrong window when another one owns the
    # foreground, so give it a few tries before calling it a failure.
    for ($i = 1; $i -le 3; $i++) {
        try {
            Invoke-ChildScript "capture_window.ps1" @{ OutFile = $Path } | Out-Null
            return
        } catch {
            if ($i -eq 3) { throw }
            Start-Sleep -Seconds 2
        }
    }
}

function Get-ScreenText([string]$Shot) {
    $text = Invoke-ChildScript "ocr_image.ps1" @{ Path = $Shot }
    return ($text -join " ").ToUpperInvariant()
}

function Wait-ForWindow($Proc, [int]$TimeoutSec) {
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        try {
            $Proc.Refresh()
            if ($Proc.HasExited) { return $false }
            if ($Proc.MainWindowHandle -ne [IntPtr]::Zero) { return $true }
        } catch { }
        Start-Sleep -Seconds 2
    }
    return $false
}

# Returns the last screen text and whether the title screen was read off it. The
# run's own exit ends the wait too: the truncated case has no window to capture
# and must not burn the whole timeout.
function Wait-ForTitle($Proc, [string]$Shot, [int]$TimeoutSec) {
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    $text = ""
    while ((Get-Date) -lt $deadline) {
        try { $Proc.Refresh(); if ($Proc.HasExited) { break } } catch { break }
        try {
            Capture-To $Shot
            $text = Get-ScreenText $Shot
        } catch {
            Start-Sleep -Seconds 3
            continue
        }
        if ($text.Contains("TO START")) { break }
        Start-Sleep -Seconds 3
    }
    return [pscustomobject]@{ Text = $text; Seen = $text.Contains("TO START") }
}

function Get-NewestLog {
    $f = Get-ChildItem (Join-Path $logs "*.log") -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $f) { return $null }
    return $f.FullName
}

# The refusal is written to the log just before the loader's modal dialog goes up,
# and a refused image never gets a title screen, so this is what the corrupt cases
# wait on - a window and a title would only be waited for in vain.
function Wait-ForRefusal($Proc, [int]$TimeoutSec) {
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        foreach ($f in (Get-ChildItem (Join-Path $logs "*.log") -ErrorAction SilentlyContinue)) {
            if ((Get-LogText $f.FullName) -match 'Refusing|Failed to map') { return $true }
        }
        try { $Proc.Refresh(); if ($Proc.HasExited) { return $true } } catch { return $true }
        Start-Sleep -Seconds 2
    }
    return $false
}

# The refusal line's match, or $null when the line does not have the shape the
# case expects. [regex] rather than -match: -match sets $Matches as a side effect,
# which a chain of elseif tests would quietly overwrite.
function Get-RefusalMatch([string]$Line, [string]$Pattern) {
    if (-not $Line) { return $null }
    $m = [regex]::Match($Line, $Pattern)
    if ($m.Success) { return $m }
    return $null
}

function Get-LogText([string]$Path) {
    if (-not $Path -or -not (Test-Path $Path)) { return "" }
    # The log may still be flushing as the process dies; a sharing violation here
    # is expected and not a run failure.
    for ($i = 0; $i -lt 5; $i++) {
        try { return (Get-Content -LiteralPath $Path -Raw) } catch { Start-Sleep -Milliseconds 200 }
    }
    return ""
}

# ------------------------------------------------------------------ cases ---

$cases = @(
    [pscustomobject]@{ Name = "retail"; Image = $retailCopy; Note = "the recorded retail image"; Refusal = "" }
    [pscustomobject]@{ Name = "payload"; Image = $PayloadImage; Note = "a valid image with a different digest (the Ultimate payload)"; Refusal = "" }
    [pscustomobject]@{ Name = "truncated"; Image = $truncatedImage; Note = "the first 1 MiB of the retail image: the data behind the header is cut off"; Refusal = 'its block table describes (\d+) bytes of image data, the image holds (\d+) after its (\d+)-byte header' }
    [pscustomobject]@{ Name = "short"; Image = $shortImage; Note = "the first 2 KiB of the retail image: the header itself is cut off"; Refusal = 'its header claims (\d+) bytes, the image holds (\d+)' }
    [pscustomobject]@{ Name = "empty"; Image = $emptyImage; Note = "a zero-length file"; Refusal = 'Failed to map .+ \(0 bytes\) for module' }
)

$rows = @()
$restoreOk = $false

try {
    foreach ($case in $cases) {
        if (-not (Test-Path $case.Image)) {
            Write-Host ("{0,-9}: skipped - no image at {1}" -f $case.Name, $case.Image)
            $rows += [pscustomobject]@{
                Case = $case.Name; Note = $case.Note; Image = "-"; GateExit = "-"
                Identity = "-"; Boot = "-"; Refusal = ""; Verdict = "skip"; Failures = @()
            }
            continue
        }

        Copy-Item -LiteralPath $case.Image -Destination $imagePath -Force
        $size = (Get-Item -LiteralPath $imagePath).Length
        $digest = Get-Digest $imagePath
        $digestHead = $digest.Substring(0, 12)
        $gateLog = Join-Path $capDir ($case.Name + "-gate.txt")

        # The build half's verdict on the very same file. A failing gate writes
        # its mismatch to stderr, and PowerShell 5.1 turns native stderr into an
        # error record that $ErrorActionPreference = "Stop" would raise as a
        # terminating error - which is why the expectation is relaxed around the
        # call and the tool's own text is read back from the file afterwards.
        $relaxed = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        try {
            & $gateExe --check --root $GameRoot --fingerprints $fingerprintFile *> $gateLog
            $gateExit = $LASTEXITCODE
        } finally {
            $ErrorActionPreference = $relaxed
        }
        $gateText = if (Test-Path $gateLog) { (Get-Content -LiteralPath $gateLog -Raw).Trim() -replace "\s+", " " } else { "" }
        # `-join ''` on the way out: Get-Content hands back strings carrying the
        # provider's own note properties (PSPath, ReadCount, Length), and those are
        # what a JSON dump of the verdict line would otherwise be made of.
        $gateLine = ((@((Get-Content -LiteralPath $gateLog -ErrorAction SilentlyContinue) |
            Where-Object { $_ -match 'entrypoint' })[0]) -join '').Trim()
        if (-not $gateLine) { $gateLine = $gateText }

        Get-ChildItem (Join-Path $logs "*.log") -ErrorAction SilentlyContinue | Remove-Item -Force
        $shot = Join-Path $capDir ($case.Name + ".png")
        if (Test-Path $shot) { Remove-Item $shot -Force }

        $p = Start-Process -FilePath $exe -WorkingDirectory $work -PassThru `
            -ArgumentList "--game_data_root=$GameRoot", "--ultimate_mode=0", `
                "--log_level=debug", "--log_flush_interval=1", "--log_max_file_size_mb=100"
        if ($case.Refusal) {
            # Nothing to see: the loader refused the image before any guest ran.
            $title = [pscustomobject]@{ Text = ""; Seen = $false }
            if (-not (Wait-ForRefusal $p $TitleTimeoutSec)) {
                Write-Host ("  ({0}: no refusal line within {1}s)" -f $case.Name, $TitleTimeoutSec)
            }
        } else {
            if (-not (Wait-ForWindow $p $TitleTimeoutSec)) {
                Write-Host ("  ({0}: no window within {1}s)" -f $case.Name, $TitleTimeoutSec)
            }
            $title = Wait-ForTitle $p $shot $TitleTimeoutSec
        }

        $exitedOnItsOwn = $p.HasExited
        $exitCode = "-"
        $cleanClose = $false
        if ($exitedOnItsOwn) {
            $p.WaitForExit()
            $exitCode = $p.ExitCode
        } else {
            $p.CloseMainWindow() | Out-Null
            if ($p.WaitForExit($CloseWaitSec * 1000)) { $cleanClose = $true; $exitCode = $p.ExitCode }
            else { Stop-Process -Id $p.Id -Force }
        }

        $logPath = Get-NewestLog
        $text = Get-LogText $logPath
        if ($logPath) { Copy-Item -LiteralPath $logPath -Destination (Join-Path $capDir ($case.Name + ".log")) -Force }

        $identitySeen = [bool]($text -match 'game data identity:')
        $modified = [bool]($text -match 'game data identity: MODIFIED')
        $cannotRead = [bool]($text -match 'game data identity: cannot read')
        $fatal = [bool]($text -match '\[FATAL\]')
        $expectedLine = [regex]::Match($text, '(?m)^.*expected: \d+ bytes.*$').Value.Trim()
        # Both shapes of the line: "MODIFIED - <path> is <n> bytes, sha256 <h>" and
        # the recorded "<name> <version> (<n> bytes, sha256 <h>)". `.*?` stops at
        # the first size/digest pair, which is the reported one in both cases and
        # never the "expected:" line's (it is on the next line, and `.` does not
        # cross a newline).
        $reportedSize = "-"
        $reportedDigest = "-"
        $m = [regex]::Match($text, 'game data identity: .*?(\d+) bytes, sha256 ([0-9a-f]{64})')
        if ($m.Success) { $reportedSize = [int]$m.Groups[1].Value; $reportedDigest = $m.Groups[2].Value }

        $lines = @($text -split "`r?`n" | Where-Object { $_.Trim() -ne "" })
        $lastLine = if ($lines.Count -gt 0) { $lines[-1].Trim() } else { "" }
        # The line that says why a corrupt image was refused. Two shapes: the XEX
        # loader's own ("Refusing damaged XEX image <path>: <claim>") and the module
        # loader's ("Refusing to load <path>: ..." / "Failed to map ...").
        $refusalLine = ((@($lines | Where-Object { $_ -match '\[error\].*(Refusing|Failed to map )' })[0]) -join '').Trim()
        # ... and the same line without its [time] [level] [category] [thread] prefix.
        $refusalText = ($refusalLine -replace '^\[[^\]]*\] \[[^\]]*\] \[[^\]]*\] \[[^\]]*\] ', '').Trim()

        $fail = New-Object System.Collections.ArrayList
        switch ($case.Name) {
            "retail" {
                if (-not $identitySeen) { [void]$fail.Add("no 'game data identity' line in the log") }
                if ($modified) { [void]$fail.Add("the recorded image was reported MODIFIED") }
                if ($reportedSize -ne $expectedSize -or $reportedDigest -ne $expectedSha) {
                    [void]$fail.Add("the line does not name the recorded digest (got $reportedSize bytes, $reportedDigest)")
                }
                if ($gateExit -ne 0) { [void]$fail.Add("the build gate exited $gateExit on the recorded image") }
                if (-not $title.Seen) { [void]$fail.Add("the title screen ('TO START') was not recognised") }
                if (-not $cleanClose) { [void]$fail.Add("the window did not close cleanly") }
            }
            "payload" {
                if (-not $modified) { [void]$fail.Add("no MODIFIED warning for a valid image with a different digest") }
                if ($reportedSize -ne $size -or $reportedDigest -ne $digest) {
                    [void]$fail.Add("the warning does not name the file it found (got $reportedSize bytes, $reportedDigest; expected $size bytes, $digest)")
                }
                if ($expectedLine -notmatch [regex]::Escape($expectedSha)) {
                    [void]$fail.Add("the 'expected:' line does not carry the recorded digest")
                }
                if ($expectedLine -notmatch 'config/game_fingerprints.toml') {
                    [void]$fail.Add("the 'expected:' line does not point at the record")
                }
                if ($gateExit -eq 0) { [void]$fail.Add("the build gate accepted an image that does not match the record") }
                if (-not $title.Seen) { [void]$fail.Add("the boot did not continue past the warning to the title screen") }
                if (-not $cleanClose) { [void]$fail.Add("the window did not close cleanly") }
            }
            "truncated" {
                if ($identitySeen) { [void]$fail.Add("an identity line was written for an image that does not load") }
                if ($title.Seen) { [void]$fail.Add("the title screen was reached with an image that does not load") }
                if ($gateExit -eq 0) { [void]$fail.Add("the build gate accepted a truncated image") }
                if ($exitCode -eq 0) { [void]$fail.Add("the run exited 0 after failing to load the image") }
                $m = Get-RefusalMatch $refusalLine $case.Refusal
                if (-not $m) {
                    [void]$fail.Add("the refusal does not name the block table running past the image (got: '$refusalText')")
                } elseif ([int]$m.Groups[1].Value -le [int]$m.Groups[2].Value -or
                          [int]$m.Groups[2].Value -ne ($size - $xexHeaderSize) -or
                          [int]$m.Groups[3].Value -ne $xexHeaderSize) {
                    [void]$fail.Add("the refusal's numbers do not describe this image (got '$refusalText'; $size bytes and a $xexHeaderSize-byte header)")
                }
            }
            "short" {
                if ($identitySeen) { [void]$fail.Add("an identity line was written for an image whose header is cut off") }
                if ($title.Seen) { [void]$fail.Add("the title screen was reached with an image whose header is cut off") }
                if ($gateExit -eq 0) { [void]$fail.Add("the build gate accepted a truncated header") }
                if ($exitCode -eq 0) { [void]$fail.Add("the run exited 0 after failing to load the image") }
                $m = Get-RefusalMatch $refusalLine $case.Refusal
                if (-not $m) {
                    [void]$fail.Add("the refusal does not name the header running past the image (got: '$refusalText')")
                } elseif ([int]$m.Groups[1].Value -ne $xexHeaderSize -or [int]$m.Groups[2].Value -ne $size) {
                    [void]$fail.Add("the refusal's numbers do not describe this image (got '$refusalText'; $size bytes and a $xexHeaderSize-byte header)")
                }
            }
            "empty" {
                if ($identitySeen) { [void]$fail.Add("an identity line was written for a zero-length image") }
                if ($title.Seen) { [void]$fail.Add("the title screen was reached from a zero-length image") }
                if ($gateExit -eq 0) { [void]$fail.Add("the build gate accepted a zero-length image") }
                if ($exitCode -eq 0) { [void]$fail.Add("the run exited 0 after failing to read the image") }
                if (-not (Get-RefusalMatch $refusalLine $case.Refusal)) {
                    [void]$fail.Add("the run does not say the empty file could not be mapped (got: '$refusalText')")
                }
            }
        }
        if ($fatal) { [void]$fail.Add("the log contains a [FATAL]") }
        # The check runs after the image loaded, so its branch for an unreadable
        # image should be unreachable here; if it fires, the boot loaded one file
        # and measured another.
        if ($cannotRead) { [void]$fail.Add("the check could not read the image the boot had just loaded") }

        if ($case.Name -eq "retail") { $identity = if ($identitySeen) { "recorded" } else { "none" } }
        elseif ($modified) { $identity = "MODIFIED" }
        elseif ($cannotRead) { $identity = "cannot read" }
        else { $identity = "none" }
        $boot = if ($title.Seen) { "title" } elseif ($case.Refusal) { "refused" } elseif ($exitedOnItsOwn) { "exited" } elseif ($cleanClose) { "closed" } else { "killed" }
        $verdict = if ($fail.Count -eq 0) { "pass" } else { "FAIL" }

        Write-Host ("{0,-9}: gate {1}  identity {2,-11} boot {3,-7} exit {4}  -> {5}" -f `
            $case.Name, $gateExit, $identity, $boot, $exitCode, $verdict)
        Write-Host ("            gate: {0}" -f ($gateLine -replace " +", " "))
        if ($refusalText) { Write-Host ("            refusal: {0}" -f $refusalText) }
        foreach ($f in $fail) { Write-Host ("  !! {0}" -f $f) }

        $rows += [pscustomobject]@{
            Case          = $case.Name
            Note          = $case.Note
            Image         = "$size bytes, sha256 $digestHead..."
            GateExit      = $gateExit
            GateVerdict   = $gateLine
            Identity      = $identity
            IdentityLine  = if ($identitySeen) { (@($lines | Where-Object { $_ -match 'game data identity:' })[0]).Trim() } else { "" }
            ExpectedLine  = $expectedLine
            Reported      = "$reportedSize bytes, sha256 $reportedDigest"
            Refusal       = $refusalText
            Boot          = $boot
            LastLine      = $lastLine
            ExitCode      = $exitCode
            CleanClose    = $cleanClose
            Fatal         = $fatal
            Verdict       = $verdict
            Failures      = @($fail)
        }
    }
} finally {
    # The dump is not this script's to leave modified, whatever happened above.
    if (Test-Path $retailCopy) {
        Copy-Item -LiteralPath $retailCopy -Destination $imagePath -Force
        $back = Get-Digest $imagePath
        $restoreOk = ($back -eq $expectedSha)
        if ($restoreOk) {
            Write-Host "`nrestored $imagePath (sha256 $back)"
        } else {
            Write-Host "`n!! $imagePath is $back - the recorded image is $expectedSha. Re-copy it from the dump."
        }
    }
}

# ----------------------------------------------------------------- result ---

$executed = @($rows | Where-Object { $_.Verdict -ne "skip" })
$failed = @($executed | Where-Object { $_.Verdict -ne "pass" })

Write-Host "`n=== wrong-data acceptance ==="
$rows | Select-Object Case, Image, GateExit, Identity, Boot, Verdict |
    Format-Table -AutoSize | Out-String | Write-Host
Write-Host ("build half (gate): " + (($rows | Where-Object { $_.GateExit -ne "-" } | ForEach-Object { "$($_.Case) exit $($_.GateExit)" }) -join ", "))

$summary = [pscustomobject]@{
    timestamp  = (Get-Date).ToString("s")
    game_root  = $GameRoot
    build_dir  = $BuildDir
    expected   = [pscustomobject]@{ path = "default.xex"; size = $expectedSize; sha256 = $expectedSha }
    restored   = $restoreOk
    passed     = $executed.Count - $failed.Count
    failed     = $failed.Count
    skipped    = $rows.Count - $executed.Count
    cases      = $rows
}
$summary | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $capDir "summary.json") -Encoding UTF8
Write-Host ("cases: {0} executed, {1} passed, {2} failed, {3} skipped - evidence in $capDir" -f `
    $executed.Count, ($executed.Count - $failed.Count), $failed.Count, ($rows.Count - $executed.Count))

if ($failed.Count -gt 0 -or -not $restoreOk) { exit 1 }
exit 0
