# Milestone 4 acceptance: local settings/save persistence.
#
# The milestone asks for a deterministic local profile/storage response and for
# defined behaviour when that storage is missing or corrupt. Every case runs the
# title against a *fresh* --user_data_root under out/, so what is checked is our
# storage layer and not whatever the machine's Documents\rb_blitz happens to
# hold. That isolation only works because the launcher's data-root overrides are
# honoured now (they used to be discarded silently - see docs/bringup-log.md,
# 2026-09-20, and the regression test tests/path_policy_tests.cpp).
#
# Cases, in order, sharing one writable root:
#
#   fresh            nothing there yet: the title's own opens of globaloptions:
#                    and songcache: have to *create* the deterministic content
#                    tree by writing a default payload into each
#   restart          a second process over the same root: same read evidence, and
#                    the bytes the first process left are neither rewritten nor
#                    lost (content-file hashes compared before/after)
#   missing          the whole root deleted: recreated with the same file set as
#                    `fresh` had, i.e. the layout is deterministic
#   corrupt-payload  the globaloptions payload overwritten with 0xAB: the boot
#                    has to stay non-fatal and still reach the menus
#   corrupt-header   the globaloptions content *header* likewise (that is the
#                    file which describes the payload to the SDK)
#
# Evidence per case: the run log (trace level, so the VFS reports the guest's own
# file operations), a screenshot of the screen the case asserted on, an inventory
# of the writable root, and the before/after hashes. "The title used its settings"
# is not "the file exists" but the operations the log attributes to the handle the
# title opened `globaloptions:\globaloptions` with:
#
#   [NtCreateFile] path=globaloptions:\globaloptions access=0x80100080 ... disp=0x1
#   [NtCreateFile] -> 0x0 handle=0xf80000f0
#   [NtReadFile] handle=0xf80000f0 ... len=0x400 offset=-1
#   [NtReadFile] -> 0x0 (sync=true, iosb_status=0x0, iosb_info=1024, ...)
#   [NtClose] handle=0xf80000f0
#
# i.e. the title opened its settings and moved the whole 1024 bytes that were on
# disk. `songcache` goes through the same pattern but is additionally probed first
# (`access=0x100080 ... disp=0x1`, no read), so the number of opens per file is
# reported rather than asserted. A root that does not exist yet is the other half
# of the same behaviour: there the title opens with disp=0x5 (create/overwrite)
# and *writes* a fresh default payload instead of reading one. Reads and writes are
# therefore both reported per file, and both count as "the title used this file".
# The corrupt cases deliberately do not require the damaged file to be restored -
# whatever the title does with it is reported instead.
#
# NtWriteFile carries no trace call in the pinned SDK, so the [NtWriteFile] lines
# this script reads only exist after
# patches/rexglue-sdk/0003-trace-ntwritefile-and-scatter-reads.patch, which also
# traces NtReadFileScatter (apply with scripts/apply_sdk_patches.ps1).
#
# Usage:
#   .\scripts\acceptance_persistence.ps1
#   .\scripts\acceptance_persistence.ps1 -Only fresh,restart
#   .\scripts\acceptance_persistence.ps1 -UserDataRoot out\my-scratch
#
# Writes logs, screens, inventories and summary.json to out/m4-persistence/,
# prints a pass/fail table and exits non-zero if any case failed.
param(
    [string]$BuildDir = "out/build/win-amd64-release",
    [string]$OutDir = "out/m4-persistence",
    [string]$GameRoot,
    # Isolated writable root, so a run never touches the machine's own profile.
    [string]$UserDataRoot,
    [string]$UltimateMode = "0",
    # Content the SDK lays out under <root>\<XUID>\<title id>: the XUID is a
    # hardcoded SDK constant (rexglue-sdk/src/system/xam/user_profile.cpp), the
    # title id is Rock Band Blitz's, and 1 is the save-game content type.
    [string]$Xuid = "B13EBABEBABEBABE",
    [string]$TitleId = "5841122D",
    [string]$ContentType = "00000001",
    [string[]]$Only,
    [int]$BootTimeoutSec = 180,
    [int]$CloseWaitSec = 30
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$work = Join-Path $root $BuildDir
$exe = Join-Path $work "rb_blitz.exe"
$logs = Join-Path $work "logs"
if (-not $GameRoot) { $GameRoot = Join-Path $root "game" }
$capDir = Join-Path $root $OutDir
if (-not $UserDataRoot) { $UserDataRoot = Join-Path $capDir "userdata" }
New-Item -ItemType Directory -Force -Path $capDir | Out-Null

if (-not (Test-Path $exe)) { throw "not found: $exe" }
if (-not (Test-Path (Join-Path $GameRoot "default.xex"))) { throw "no default.xex under $GameRoot" }

# Everything the title owns, relative to the writable root - the base the
# inventories, hashes and size maps below are all keyed by.
$contentPrefix = "$Xuid\$TitleId"
$contentDir = Join-Path $UserDataRoot $contentPrefix
$settingsRel = "$contentPrefix\$ContentType\globaloptions\globaloptions"
$songcacheRel = "$contentPrefix\$ContentType\songcache\songcache"
$settingsHeaderRel = "$contentPrefix\Headers\$ContentType\globaloptions.header"
$songcacheHeaderRel = "$contentPrefix\Headers\$ContentType\songcache.header"
$expected = @($settingsRel, $songcacheRel, $settingsHeaderRel, $songcacheHeaderRel)

# ------------------------------------------------------------------ helpers ---

# Anything that runs another .ps1 goes through a child PowerShell: this machine's
# execution policy is Restricted (docs/build-and-run.md §0).
function Invoke-ChildScript([string]$Script, [hashtable]$Named) {
    $argv = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $PSScriptRoot $Script))
    foreach ($k in $Named.Keys) { $argv += @("-$k", [string]$Named[$k]) }
    $out = & powershell @argv 2>&1
    if ($LASTEXITCODE -ne 0) { throw "$Script failed: $out" }
    return $out
}

function Get-NewestLog {
    $f = Get-ChildItem (Join-Path $logs "*.log") -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $f) { return $null }
    return $f.FullName
}

function Read-LogLines([string]$Path) {
    if (-not $Path -or -not (Test-Path $Path)) { return @() }
    # The log is still being written while we read it, so a sharing violation
    # here is expected rather than a run failure.
    for ($i = 0; $i -lt 5; $i++) {
        try { return @(Get-Content -LiteralPath $Path) } catch { Start-Sleep -Milliseconds 200 }
    }
    return @()
}

# Strip the log's timestamp/level/thread prefix so reported lines stay readable.
function Show-Line([string]$Line) {
    return ($Line -replace "^\[[^\]]+\] \[(\w+)\] \[[\w:]+\] \[t\d+\] *", "[`$1] ")
}

function Get-Relative([string]$Base, [string]$Path) {
    return $Path.Substring($Base.Length).TrimStart("\")
}

# The writable root as a sorted list of relative paths, so two runs can be
# compared as sets rather than one expected file at a time.
function Get-Inventory([string]$Dir) {
    if (-not (Test-Path $Dir)) { return @() }
    return @(Get-ChildItem $Dir -Recurse -File -ErrorAction SilentlyContinue |
        ForEach-Object { Get-Relative $Dir $_.FullName } | Sort-Object)
}

function Get-Hashes([string]$Dir) {
    $map = @{}
    if (-not (Test-Path $Dir)) { return $map }
    foreach ($f in Get-ChildItem $Dir -Recurse -File -ErrorAction SilentlyContinue) {
        $map[(Get-Relative $Dir $f.FullName)] = (Get-FileHash -LiteralPath $f.FullName -Algorithm SHA256).Hash
    }
    return $map
}

function Get-Sizes([string]$Dir) {
    $map = @{}
    if (-not (Test-Path $Dir)) { return $map }
    foreach ($f in Get-ChildItem $Dir -Recurse -File -ErrorAction SilentlyContinue) {
        $map[(Get-Relative $Dir $f.FullName)] = $f.Length
    }
    return $map
}

function Invoke-Actions([string[]]$Actions) {
    Invoke-ChildScript "drive_ui.ps1" @{
        Actions = ($Actions -join ","); BuildDir = $BuildDir; OutDir = $OutDir
    } | Out-Null
}

function Get-ScreenText([string]$Path) {
    if (-not (Test-Path $Path)) { return "<no screenshot: $Path>" }
    return (($(Invoke-ChildScript "ocr_image.ps1" @{ Path = $Path })) -join " ").ToUpperInvariant()
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

function Start-RbBlitz {
    Get-ChildItem (Join-Path $logs "*.log") -ErrorAction SilentlyContinue | Remove-Item -Force
    return Start-Process -FilePath $exe -WorkingDirectory $work -PassThru -ArgumentList `
        "--game_data_root=$GameRoot", "--ultimate_mode=$UltimateMode", `
        "--user_data_root=$UserDataRoot", "--log_noisy=true", "--log_level=trace"
}

function Stop-RbBlitz($Proc, [int]$TimeoutSec) {
    if ($Proc.HasExited) { return $false }
    $Proc.CloseMainWindow() | Out-Null
    if (-not $Proc.WaitForExit($TimeoutSec * 1000)) {
        Stop-Process -Id $Proc.Id -Force
        Start-Sleep -Seconds 2
        return $false
    }
    return $true
}

# The three dialogs in front of the offline menus are dismissed with SELECT (the
# title screen, "cannot connect to Rock Central", "proceed in Offline Mode?").
# The title opens its content files a few seconds after the menu appears.
function Reach-OfflineMenus($Proc, [string]$Tag, $Notes) {
    if (-not (Wait-ForWindow $Proc $BootTimeoutSec)) {
        $Notes.Add("no game window appeared") | Out-Null
        return $false
    }
    $shot = "$Tag-menu.png"
    Invoke-Actions @("wait:3", "key:a", "wait:3", "key:a", "wait:3", "key:a", "wait:6", "shot:$shot")
    $text = Get-ScreenText (Join-Path $capDir $shot)
    if ($text.Contains("PLAY")) { return $true }
    # The dialogs are not always up when the window appears; one more round is
    # cheaper than failing a case over a slow boot.
    Invoke-Actions @("key:a", "wait:4", "key:a", "wait:6", "shot:$shot")
    $text = Get-ScreenText (Join-Path $capDir $shot)
    if ($text.Contains("PLAY")) { return $true }
    $Notes.Add("main menu not recognised ($text)") | Out-Null
    return $false
}

# Every open of one content file the title made, with the reads and writes it
# performed on the handle that open returned. Returns one object per open:
#
#   Access/Disposition  the NtCreateFile arguments (0x80100080 = read/write data,
#                       0x40100080 = write-only; disp 0x1 = open, 0x5 = create)
#   Reads/ReadAsked/ReadBytes    count, bytes requested, bytes delivered
#   Writes/WriteAsked/WriteBytes same for writes
#
# Reads and writes are matched to the handle, so unrelated file traffic cannot be
# mistaken for content traffic, and the scan stops at NtClose.
function Get-ContentIO([string[]]$Lines, [string]$Name) {
    $open = "\[NtCreateFile\] path=" + $Name + ":\\" + $Name +
            " access=(0x[0-9a-fA-F]+).* disp=(0x[0-9a-fA-F]+)"
    $result = New-Object System.Collections.ArrayList
    for ($i = 0; $i -lt $Lines.Count; $i++) {
        if ($Lines[$i] -notmatch $open) { continue }
        $access = $Matches[1]; $disp = $Matches[2]
        $handle = $null
        for ($j = $i + 1; $j -lt [Math]::Min($i + 6, $Lines.Count); $j++) {
            if ($Lines[$j] -match "\[NtCreateFile\] -> 0x0 handle=(0x[0-9a-fA-F]+)") {
                $handle = $Matches[1]; break
            }
        }
        $reads = 0; $readAsked = 0; $readBytes = 0
        $writes = 0; $writeAsked = 0; $writeBytes = 0
        if ($handle) {
            $limit = [Math]::Min($i + 40, $Lines.Count)
            for ($j = $i + 1; $j -lt $limit; $j++) {
                if ($Lines[$j] -match "\[NtClose\] handle=$handle") { break }
                if ($Lines[$j] -match "\[NtReadFile\] handle=$handle .* len=0x([0-9a-fA-F]+)") {
                    $reads++; $readAsked += [Convert]::ToInt32($Matches[1], 16)
                    if ($j + 1 -lt $Lines.Count -and
                        $Lines[$j + 1] -match "\[NtReadFile\] -> 0x0 \(sync=\w+, iosb_status=0x0, iosb_info=(\d+)") {
                        $readBytes += [int]$Matches[1]
                    }
                }
                if ($Lines[$j] -match "\[NtWriteFile\] handle=$handle .* len=0x([0-9a-fA-F]+)") {
                    $writes++; $writeAsked += [Convert]::ToInt32($Matches[1], 16)
                    if ($j + 1 -lt $Lines.Count -and
                        $Lines[$j + 1] -match "\[NtWriteFile\] -> 0x0 \(iosb_status=0x0, iosb_info=(\d+)") {
                        $writeBytes += [int]$Matches[1]
                    }
                }
            }
        }
        $result.Add([pscustomobject]@{
            Access = $access; Disposition = $disp; Handle = $handle
            Reads = $reads; ReadAsked = $readAsked; ReadBytes = $readBytes
            Writes = $writes; WriteAsked = $writeAsked; WriteBytes = $writeBytes
        }) | Out-Null
    }
    return $result
}

# "R 1024/1024 B, W 0/0 B over 2 opens" - the one-line form the table shows.
function Format-ContentIO($Opens) {
    if (-not $Opens -or $Opens.Count -eq 0) { return "" }
    $r = [int](($Opens | Measure-Object -Property ReadBytes -Sum).Sum)
    $w = [int](($Opens | Measure-Object -Property WriteBytes -Sum).Sum)
    $rq = [int](($Opens | Measure-Object -Property ReadAsked -Sum).Sum)
    $wq = [int](($Opens | Measure-Object -Property WriteAsked -Sum).Sum)
    # @(): a single open comes back as a bare object, whose .Count is empty in
    # Windows PowerShell rather than 1.
    return ("R {0}/{1} B, W {2}/{3} B over {4} open(s)" -f $r, $rq, $w, $wq, @($Opens).Count)
}

# The delivered read byte count, which is what tells the restart case apart: a
# title that read a fresh default would not report the size this root held.
function Get-ReadBytes($Opens) {
    if (-not $Opens -or @($Opens).Count -eq 0) { return -1 }
    return [int](($Opens | Measure-Object -Property ReadBytes -Sum).Sum)
}

# The write byte count: the fresh root's proof that the title authored defaults.
function Get-WriteBytes($Opens) {
    if (-not $Opens -or @($Opens).Count -eq 0) { return -1 }
    return [int](($Opens | Measure-Object -Property WriteBytes -Sum).Sum)
}

# ------------------------------------------------------------------- a case ---

function Invoke-Case {
    param([string]$Name, [ValidateSet("fresh", "keep")][string]$Prepare = "keep", [string]$Damage = "")

    $notes = New-Object System.Collections.ArrayList
    $row = [pscustomobject]@{
        Case = $Name; Pass = $false; Menu = $false; Settings = ""; Songcache = ""
        SettingsReadBytes = -1; SettingsWriteBytes = -1; SongcacheReadBytes = -1
        PreExisted = $false; ContentSame = $false; Writes = 0
        Fatal = $false; Clean = $false; Files = 0; Bytes = 0; LogLines = 0
        Changed = ""; Notes = ""; Inventory = @()
    }

    if ($Prepare -eq "fresh" -and (Test-Path $UserDataRoot)) {
        Remove-Item $UserDataRoot -Recurse -Force
    }
    if ($Damage) {
        foreach ($rel in @($Damage -split ";")) {
            $p = Join-Path $UserDataRoot $rel
            if (-not (Test-Path $p)) { $notes.Add("nothing to damage at $rel") | Out-Null; continue }
            $len = (Get-Item -LiteralPath $p).Length
            $bytes = New-Object byte[] $len
            for ($i = 0; $i -lt $len; $i++) { $bytes[$i] = 0xAB }
            [System.IO.File]::WriteAllBytes($p, $bytes)
            $notes.Add("overwrote $rel with $len bytes of 0xAB") | Out-Null
        }
    }

    $before = Get-Hashes $UserDataRoot
    $sizesBefore = Get-Sizes $UserDataRoot
    $row.PreExisted = $before.Count -gt 0

    $proc = Start-RbBlitz
    try {
        $row.Menu = Reach-OfflineMenus $proc $Name $notes
        # Nothing here needs a song: the offline menus are what make the title
        # open its settings and the song cache.
        Start-Sleep -Seconds 5
        $row.Clean = Stop-RbBlitz $proc $CloseWaitSec
        if (-not $row.Clean) { $notes.Add("the window close did not exit the process") | Out-Null }
    } finally {
        if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force }
    }

    $log = Get-NewestLog
    $lines = Read-LogLines $log
    $row.LogLines = $lines.Count
    $fatals = @($lines | Where-Object { $_ -match "\[FATAL\]" })
    $row.Fatal = $fatals.Count -gt 0
    if ($row.Fatal) { $notes.Add("log has [FATAL]: " + (Show-Line $fatals[0])) | Out-Null }
    if (-not ($lines | Where-Object { $_ -match "Title terminated" })) {
        $row.Clean = $false
        $notes.Add("no clean-shutdown marker in the log") | Out-Null
    }
    if (-not ($lines | Where-Object { $_ -match "User data:\s+$([regex]::Escape($UserDataRoot))" })) {
        $notes.Add("the log does not name $UserDataRoot as the user data root") | Out-Null
    }
    foreach ($n in @("globaloptions", "songcache")) {
        if (-not ($lines | Where-Object { $_ -match "Registered symbolic link: $n`: => \\Device\\Content\\" })) {
            $notes.Add("the $n`: content link was never mounted") | Out-Null
        }
    }
    $settings = Get-ContentIO $lines "globaloptions"
    $songcache = Get-ContentIO $lines "songcache"
    $row.Settings = Format-ContentIO $settings
    $row.Songcache = Format-ContentIO $songcache
    $row.SettingsReadBytes = Get-ReadBytes $settings
    $row.SettingsWriteBytes = Get-WriteBytes $settings
    $row.SongcacheReadBytes = Get-ReadBytes $songcache
    if (-not $settings) {
        $notes.Add("the title never opened globaloptions: at all") | Out-Null
    } elseif ($row.SettingsReadBytes -le 0 -and $row.SettingsWriteBytes -le 0) {
        $notes.Add("the title opened globaloptions: but moved no bytes through it") | Out-Null
    }
    if (-not $songcache) {
        $notes.Add("the title never opened songcache: at all") | Out-Null
    } elseif ($row.SongcacheReadBytes -le 0) {
        $notes.Add("the title never read songcache back") | Out-Null
    }
    $row.Writes = @($lines | Where-Object { $_ -match "\[NtWriteFile\]" }).Count

    # What the root holds now, and what this run changed in it.
    $after = Get-Hashes $UserDataRoot
    $sizesAfter = Get-Sizes $UserDataRoot
    $changed = New-Object System.Collections.ArrayList
    foreach ($k in $after.Keys) {
        if (-not $before.ContainsKey($k)) {
            $changed.Add("$k (created)") | Out-Null
        } elseif ($before[$k] -ne $after[$k]) {
            $changed.Add("$k ($($sizesBefore[$k]) -> $($sizesAfter[$k]) B)") | Out-Null
        }
    }
    foreach ($k in $before.Keys) {
        if (-not $after.ContainsKey($k)) { $changed.Add("$k (removed)") | Out-Null }
    }
    $contentChanged = @($changed | Where-Object { $expected -contains ($_ -split " ")[0] })
    $row.ContentSame = $contentChanged.Count -eq 0
    $row.Changed = (($changed | Where-Object { $expected -notcontains ($_ -split " ")[0] }) -join "; ")

    $row.Inventory = @(Get-Inventory $UserDataRoot)
    $row.Files = $row.Inventory.Count
    $row.Bytes = [int](($sizesAfter.Values | Measure-Object -Sum).Sum)
    $row.Inventory | Set-Content (Join-Path $capDir "$Name-files.txt")
    if ($log) { Copy-Item $log (Join-Path $capDir "$Name.log") -Force }

    # The delivered read byte count is what tells the restart case apart: a title
    # that read a fresh default would not report the size this root already had.
    $row | Add-Member -NotePropertyName SizeBefore -NotePropertyValue $(if ($sizesBefore.ContainsKey($settingsRel)) { $sizesBefore[$settingsRel] } else { -1 })
    $row | Add-Member -NotePropertyName SizeAfter -NotePropertyValue $(if ($sizesAfter.ContainsKey($settingsRel)) { $sizesAfter[$settingsRel] } else { -1 })
    if ($row.SettingsWriteBytes -gt 0 -and $row.SettingsWriteBytes -ne $row.SizeAfter) {
        $notes.Add("wrote $($row.SettingsWriteBytes) B but the payload is $($row.SizeAfter) B") | Out-Null
    }
    if ($notes.Count -gt 0) { $row.Notes = $notes -join "; " }
    return $row
}

# --------------------------------------------------------------------- main ---

$cases = @(
    @{ Name = "fresh"; Prepare = "fresh"; Damage = ""; Want = "created" }
    @{ Name = "restart"; Prepare = "keep"; Damage = ""; Want = "restart" }
    @{ Name = "missing"; Prepare = "fresh"; Damage = ""; Want = "created" }
    @{ Name = "corrupt-payload"; Prepare = "keep"; Damage = $settingsRel; Want = "survived" }
    @{ Name = "corrupt-header"; Prepare = "keep"; Damage = $settingsHeaderRel; Want = "survived" }
)
if ($Only) { $cases = @($cases | Where-Object { $Only -contains $_.Name }) }
if ($cases.Count -eq 0) { throw "no case matched -Only $($Only -join ',')" }

$rows = @()
$baseline = @()
foreach ($c in $cases) {
    Write-Host "=== $($c.Name) ==="
    $row = Invoke-Case -Name $c.Name -Prepare $c.Prepare -Damage $c.Damage
    $missing = @($expected | Where-Object { $row.Inventory -notcontains $_ })
    $extra = @()
    if ($c.Want -eq "created" -and $baseline.Count -gt 0) {
        $extra = @($row.Inventory | Where-Object { $baseline -notcontains $_ })
    }
    if ($missing.Count -gt 0) {
        $row.Notes = ("$($row.Notes); not in the writable root: " + ($missing -join ", ")).Trim("; ")
    }
    if ($extra.Count -gt 0) {
        $row.Notes = ("$($row.Notes); not in the 'fresh' layout: " + ($extra -join ", ")).Trim("; ")
    }

    switch ($c.Want) {
        "created" {
            # The whole deterministic content tree. There is nothing to read on a
            # root that did not exist yet, so the title's own proof is the write:
            # it authored the defaults this run.
            $row.Pass = ($missing.Count -eq 0 -and $extra.Count -eq 0 -and $row.Menu -and
                         -not $row.Fatal -and $row.Clean -and
                         ($row.SettingsReadBytes -gt 0 -or $row.SettingsWriteBytes -gt 0) -and
                         $row.SettingsWriteBytes -eq $row.SizeAfter -and $row.Songcache -ne "")
            if ($c.Name -eq "fresh") { $baseline = @($row.Inventory) }
        }
        "restart" {
            # The settings the first process left are still there byte for byte,
            # and this process read exactly those bytes.
            $row.Pass = ($row.PreExisted -and $row.ContentSame -and $missing.Count -eq 0 -and
                         $row.Menu -and -not $row.Fatal -and $row.Clean -and
                         $row.SettingsReadBytes -gt 0 -and $row.SettingsReadBytes -eq $row.SizeBefore)
            if (-not $row.PreExisted) {
                $row.Notes = ("$($row.Notes); the writable root was empty before this run - run 'fresh' first").Trim("; ")
            }
            if (-not $row.ContentSame) { $row.Notes = "$($row.Notes); the content files changed" }
            if ($row.SettingsReadBytes -ne $row.SizeBefore) {
                $row.Notes = "$($row.Notes); read $($row.SettingsReadBytes) B but the root held $($row.SizeBefore) B"
            }
        }
        "survived" {
            # No claim about repairing the damaged file: only that the title copes
            # (reads it, or replaces it with a default), and leaves the rest alone.
            $other = @($expected | Where-Object { $_ -ne $c.Damage })
            $row.Pass = ($row.Menu -and -not $row.Fatal -and $row.Clean -and
                         ($row.SettingsReadBytes -gt 0 -or $row.SettingsWriteBytes -gt 0) -and
                         @($other | Where-Object { $row.Inventory -notcontains $_ }).Count -eq 0)
        }
    }
    $rows += $row
    Write-Host ("  menu={0} settings=[{1}] songcache=[{2}] writes={3} fatal={4} clean={5} files={6} bytes={7}" -f `
        $row.Menu, $row.Settings, $row.Songcache, $row.Writes, $row.Fatal, $row.Clean, $row.Files, $row.Bytes)
}

Write-Host "`n=== summary ==="
# -Width: the natural table is wider than the default render width, which would
# wrap the last columns' headers and values.
$rows | Format-Table Case, Pass, Menu, Settings, Songcache, Writes, Fatal, Clean, ContentSame, Files, Bytes, LogLines -AutoSize |
    Out-String -Width 240 | Write-Host
foreach ($r in $rows) {
    if ($r.Changed) { Write-Host ("{0}: other writable-root changes: {1}" -f $r.Case, $r.Changed) }
    if ($r.Notes) { Write-Host ("{0}: {1}" -f $r.Case, $r.Notes) }
}
Write-Host ("writable root: {0}" -f $UserDataRoot)
Write-Host ("content tree:  {0}" -f $contentDir)

$rows | ConvertTo-Json -Depth 3 | Set-Content (Join-Path $capDir "summary.json")
$pass = @($rows | Where-Object { $_.Pass }).Count
Write-Host "persistence cases passed: $pass / $($rows.Count)"
if ($pass -ne $rows.Count) { exit 1 }
exit 0
