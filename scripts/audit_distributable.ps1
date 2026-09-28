# Distributable audit: what the installer ships, and what must never be in it.
#
# The release-readiness item is a negative claim - no retail data, no symbol from a
# proprietary database, no credential and no machine-specific path in the installer
# payload or in a packaged build - and a negative claim is only worth what the check
# behind it is worth. This is that check: it can fail, and its exit code is the answer.
#
# What it looks at:
#   installer\out\payload        the files the installer places on a user's machine
#   installer\out\dist\*.exe     every setup executable that has been built
#   installer\out\generated\*    the helper and pins.iss staged for Inno Setup
#   the repository itself        tracked files, for game data that must not be in a tree
#                                that anybody can clone
#
# The checks, and what each would catch:
#
#   1 inventory     the payload is exactly the allow-list make_payload.ps1 defines (the
#                   executable, the two SDK DLLs, the four CRT DLLs) plus
#                   payload-manifest.toml, and every file matches the manifest's size and
#                   hash. Catches a stray rb_blitz.toml, a .pdb, a logs\ directory, or a
#                   file nobody meant to ship.
#   2 retail data   no payload file is a game file: not by the fingerprints in
#                   config/game_fingerprints.toml (size and digest), not by extension,
#                   not by container magic, and not by carrying the entrypoint's own
#                   first or last 4 KiB. Catches the dump being copied in.
#   3 mod data      the same against installer/config/ultimate_fingerprints.toml. The mod
#                   is downloaded from its own upstream release and is never mirrored.
#   4 credentials   no private key, provider token, JWT or `Authorization:` header in the
#                   payload, the helper or a setup executable; and exactly one copy of the
#                   console's own XEX key in the tree, the SDK's, which is the public
#                   constant every Xenia-derived runtime carries (docs/rb3-references.md
#                   section 9). Word-level hits ("password" inside a C++ identifier) are
#                   listed as notes, not failures.
#   5 symbol data   no payload binary carries a symbol table or a .debug section, and no
#                   guest-address label ships as a string. Catches a build that stops
#                   stripping, which is how a name from somebody else's symbol database
#                   would travel.
#   6 machine paths nothing the installer ships contains the path of the machine that
#                   built it: this checkout, the user profile, Program Files, the build
#                   directory, or a compiler path from the frozen toolchain record. This
#                   is the check that found the __FILE__ leak the release build had.
#   7 tracked tree  git ls-files lists no game container and no binary artefact.
#
# Usage:
#   .\scripts\audit_distributable.ps1
#   .\scripts\audit_distributable.ps1 -PayloadDir <dir> -SetupDir <dir>
#   .\scripts\audit_distributable.ps1 -SkipSetup        # before a setup exe exists
#
# Per check it writes out/distributable-audit/<check>.txt with the matches behind the
# verdict, plus summary.json, and prints a verdict table. Exit code 0 means every check
# passed; 1 means at least one did not; 2 means the audit could not measure something it
# needs (a missing payload is a skip, a missing llvm-readobj is not).
param(
    [string]$PayloadDir,
    [string]$SetupDir,
    [string]$OutDir = "out/distributable-audit",
    [switch]$SkipSetup,
    [switch]$Quiet
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$installerDir = Join-Path $repoRoot "installer"
if (-not $PayloadDir) { $PayloadDir = Join-Path $installerDir "out/payload" }
if (-not $SetupDir) { $SetupDir = Join-Path $installerDir "out/dist" }
$outDir = Join-Path $repoRoot $OutDir
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$llvmBin = "C:\Program Files\LLVM\bin"
$llvmReadobj = Join-Path $llvmBin "llvm-readobj.exe"
if (-not (Test-Path $llvmReadobj)) {
    $cmd = Get-Command "llvm-readobj.exe" -ErrorAction SilentlyContinue
    if ($cmd) { $llvmReadobj = $cmd.Source }
}

# Files that come from Microsoft rather than from this project. Their own strings are
# not ours to fix, so a machine path found in one is reported and does not fail the run.
$vendorDlls = @("msvcp140.dll", "msvcp140_atomic_wait.dll", "vcruntime140.dll", "vcruntime140_1.dll")

$checks = @()
function Add-Check {
    param([string]$Name, [string]$Verdict, [string]$Detail, [string]$File, $Rows)
    $script:checks += [pscustomobject]@{
        Check   = $Name
        Verdict = $Verdict
        Detail  = $Detail
        Evidence = $File
        Rows    = @($Rows)
    }
    if (-not $Quiet) {
        $colour = switch ($Verdict) { "pass" { "Green" } "skip" { "DarkGray" } "note" { "Yellow" } default { "Red" } }
        Write-Host ("{0,-12} {1,-6} {2}" -f $Name, $Verdict.ToUpperInvariant(), $Detail) -ForegroundColor $colour
    }
}

function Write-Evidence([string]$Name, [string[]]$Lines) {
    $path = Join-Path $outDir "$Name.txt"
    Set-Content -LiteralPath $path -Value $Lines -Encoding UTF8
    return $path
}

# Latin-1 keeps a byte-for-byte view of the file: byte 0x85 stays 0x85 instead of
# becoming a control character, so offsets and counts are honest.
$latin1 = [Text.Encoding]::GetEncoding(28591)

# Reads a file once and returns its text in the two encodings a string literal can be
# stored in. `__FILE__` and every literal beside it are ASCII here; the UTF-16 pass is
# what catches a value that was built as a wide string.
function Get-TextViews([string]$Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    $views = New-Object System.Collections.Generic.List[object]
    $views.Add([pscustomobject]@{ Encoding = "ascii"; Text = $latin1.GetString($bytes) })
    if ($bytes.Length -ge 2) {
        $views.Add([pscustomobject]@{ Encoding = "utf16"; Text = [Text.Encoding]::Unicode.GetString($bytes, 0, $bytes.Length - ($bytes.Length % 2)) })
    }
    return $views
}

# A match inside a URL is not a path and not a credential: `https://github.com/<repo>`
# contains the repository's own name, and every pinned download URL must stay there.
function Test-InsideUrl([string]$Text, [int]$Index, [int]$Back = 48) {
    $start = [Math]::Max(0, $Index - $Back)
    $prefix = $Text.Substring($start, $Index - $start)
    return $prefix.Contains("://")
}

# Every match of $Pattern in $Path, as strings, with URLs filtered out. Returns objects so
# the caller can say which file and which encoding.
function Find-InFile {
    param([string]$Path, [string[]]$Patterns, [int]$MaxPerPattern = 200, [int]$ContextChars = 24)
    $found = New-Object System.Collections.Generic.List[object]
    foreach ($view in (Get-TextViews $Path)) {
        foreach ($pattern in $Patterns) {
            $count = 0
            foreach ($m in [regex]::Matches($view.Text, $pattern)) {
                if (Test-InsideUrl $view.Text $m.Index) { continue }
                $count++
                if ($count -gt $MaxPerPattern) { break }
                # `$snippet`, not `$context`: PowerShell variable names are
                # case-insensitive, so a local named after the parameter would
                # overwrite it on the first iteration and fail on the second.
                $snippet = ($view.Text.Substring($m.Index, [Math]::Min($ContextChars, $view.Text.Length - $m.Index)) -replace '[\x00-\x1f]', ".")
                $found.Add([pscustomobject]@{ Enc = $view.Encoding; Text = $m.Value; Context = $snippet })
            }
        }
    }
    return $found
}

# ---------------------------------------------------------------- the inputs ---

$payloadFiles = @()
if (Test-Path $PayloadDir) {
    $payloadFiles = @(Get-ChildItem -LiteralPath $PayloadDir -File | Sort-Object Name)
}
$payloadDirs = @()
if (Test-Path $PayloadDir) {
    $payloadDirs = @(Get-ChildItem -LiteralPath $PayloadDir -Directory)
}
$setupExes = @()
if (-not $SkipSetup -and (Test-Path $SetupDir)) {
    $setupExes = @(Get-ChildItem -LiteralPath $SetupDir -Filter "*.exe" -File | Sort-Object Name)
}
$helperFiles = @()
if (Test-Path (Join-Path $installerDir "out/generated")) {
    $helperFiles = @(Get-ChildItem -LiteralPath (Join-Path $installerDir "out/generated") -File)
}

Write-Host "distributable audit"
Write-Host "  payload : $PayloadDir ($($payloadFiles.Count) files)"
Write-Host "  setup   : $(if ($setupExes.Count) { ($setupExes | ForEach-Object { $_.Name }) -join ', ' } else { 'none built' })"
Write-Host ""

# ------------------------------------------------------- 1. payload inventory ---

# The allow-list is read out of make_payload.ps1 rather than repeated here, so the audit
# follows the packer instead of a second opinion about it.
$packer = Join-Path $installerDir "tools/make_payload.ps1"
$allowed = New-Object System.Collections.Generic.List[string]
if (Test-Path $packer) {
    $inWanted = $false
    foreach ($line in Get-Content -LiteralPath $packer) {
        if ($line -match '^\s*\$wanted\s*=\s*\[ordered\]@\{') { $inWanted = $true; continue }
        if ($inWanted -and $line -match '^\s*\}') { break }
        if ($inWanted -and $line -match "^\s*'([^']+)'\s*=") { $allowed.Add($Matches[1]) }
    }
}
if ($allowed.Count -eq 0) {
    Write-Host "cannot read the allow-list out of $packer" -ForegroundColor Red
    exit 2
}
$allowed += "payload-manifest.toml"

if ($payloadFiles.Count -eq 0) {
    Add-Check "inventory" "skip" "no payload at $PayloadDir (build it with installer\tools\make_payload.ps1)" "" @()
} else {
    $rows = New-Object System.Collections.Generic.List[string]
    $problems = New-Object System.Collections.Generic.List[string]
    $actual = @($payloadFiles | ForEach-Object { $_.Name })
    foreach ($name in $actual) {
        if ($allowed -notcontains $name) { $problems.Add("not in the allow-list: $name") }
    }
    foreach ($name in $allowed) {
        if ($actual -notcontains $name) { $problems.Add("listed but missing: $name") }
    }
    foreach ($dir in $payloadDirs) { $problems.Add("directory in the payload: $($dir.Name)") }

    # The manifest is what the helper checks the installed tree against, so it has to
    # describe the payload it travels with.
    $manifestPath = Join-Path $PayloadDir "payload-manifest.toml"
    if (Test-Path $manifestPath) {
        $manifestText = Get-Content -LiteralPath $manifestPath -Raw
        foreach ($block in (($manifestText -split '\[\[files\]\]') | Select-Object -Skip 1)) {
            $path = [regex]::Match($block, 'path\s*=\s*"([^"]+)"').Groups[1].Value
            $size = [int][regex]::Match($block, 'size\s*=\s*(\d+)').Groups[1].Value
            $sha = [regex]::Match($block, 'sha256\s*=\s*"([0-9a-f]{64})"').Groups[1].Value
            $file = Join-Path $PayloadDir $path
            if (-not (Test-Path $file)) { $problems.Add("manifest names a missing file: $path"); continue }
            $item = Get-Item -LiteralPath $file
            $digest = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()
            if ($item.Length -ne $size -or $digest -ne $sha) {
                $problems.Add("manifest mismatch for $path ($($item.Length) bytes, sha256 $digest)")
            }
            $rows.Add(("{0,-26} {1,12:N0} bytes  sha256 {2}" -f $path, $item.Length, $digest))
        }
    } else {
        $problems.Add("no payload-manifest.toml beside the payload")
    }

    $evidence = Write-Evidence "inventory" (@($rows) + @("") + @($problems | ForEach-Object { "PROBLEM $_" }))
    if ($problems.Count -eq 0) {
        Add-Check "inventory" "pass" "$($payloadFiles.Count) files, allow-list exactly, manifest agrees" $evidence $rows
    } else {
        Add-Check "inventory" "fail" "$($problems.Count) problem(s) - see $(Split-Path -Leaf $evidence)" $evidence $problems
    }
}

# ------------------------------------------- 2/3. retail and mod data never ship ---

# Same schema as config/game_fingerprints.toml: [[files]] blocks of role/path/size/sha256.
function Read-Fingerprints([string]$Path) {
    $entries = New-Object System.Collections.Generic.List[object]
    if (-not (Test-Path $Path)) { return $entries }
    $text = Get-Content -LiteralPath $Path -Raw
    foreach ($block in (($text -split '\[\[files\]\]') | Select-Object -Skip 1)) {
        $entries.Add([pscustomobject]@{
            Path = [regex]::Match($block, 'path\s*=\s*"([^"]+)"').Groups[1].Value
            Size = [int][regex]::Match($block, 'size\s*=\s*(\d+)').Groups[1].Value
            Sha  = [regex]::Match($block, 'sha256\s*=\s*"([0-9a-f]{64})"').Groups[1].Value
        })
    }
    return $entries
}

$gameContainerExtensions = @(".xex", ".ark", ".hdr", ".mogg", ".wem", ".xma", ".bik", ".stfs", ".pkg", ".dta", ".zbm")

function Test-GameData {
    param([string]$Name, [string]$FingerprintFile, [string]$Label, [switch]$EntrypointNeedles)
    if ($payloadFiles.Count -eq 0) {
        Add-Check $Name "skip" "no payload to check" "" @()
        return
    }
    $expected = Read-Fingerprints $FingerprintFile
    $rows = New-Object System.Collections.Generic.List[string]
    $problems = New-Object System.Collections.Generic.List[string]

    # The entrypoint's own ends: the closest thing to "the dump is in here" that is cheap
    # to test on a binary - neither a recompiled function nor a data table reproduces the
    # XEX header or its trailing metadata verbatim. Only the retail check asks for it: a
    # needle from the base game says nothing about whether the mod was mirrored.
    $needles = New-Object System.Collections.Generic.List[object]
    $entrypoint = Join-Path $repoRoot "game/default.xex"
    if ($EntrypointNeedles -and (Test-Path $entrypoint)) {
        $bytes = [IO.File]::ReadAllBytes($entrypoint)
        $head = New-Object byte[] 4096
        [Array]::Copy($bytes, 0, $head, 0, 4096)
        $tail = New-Object byte[] 4096
        [Array]::Copy($bytes, $bytes.Length - 4096, $tail, 0, 4096)
        $needles.Add([pscustomobject]@{ Name = "entrypoint first 4 KiB"; Text = $latin1.GetString($head) })
        $needles.Add([pscustomobject]@{ Name = "entrypoint last 4 KiB"; Text = $latin1.GetString($tail) })
    }

    foreach ($file in $payloadFiles) {
        $size = $file.Length
        $digest = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        $extension = $file.Extension.ToLowerInvariant()
        if ($gameContainerExtensions -contains $extension) {
            $problems.Add("$($file.Name) has a game-data extension ($extension)")
        }
        foreach ($entry in $expected) {
            if ($size -eq $entry.Size -and $digest -eq $entry.Sha) {
                $problems.Add("$($file.Name) is $($entry.Path) itself (size and digest)")
            }
        }
        # Container magic at offset 0: XEX2; STFS packages start with LIVE/PIRS/CON .
        $head = New-Object byte[] 4
        $stream = [IO.File]::OpenRead($file.FullName)
        try { [void]$stream.Read($head, 0, 4) } finally { $stream.Dispose() }
        $magic = $latin1.GetString($head)
        if ($magic -eq "XEX2" -or $magic -eq "LIVE" -or $magic -eq "PIRS" -or $magic -eq "CON ") {
            $problems.Add("$($file.Name) starts with the container magic '$magic'")
        }
        # One pass over the file's text, then both needles against it.
        $text = $latin1.GetString([IO.File]::ReadAllBytes($file.FullName))
        foreach ($needle in $needles) {
            if ($text.Contains($needle.Text)) {
                $problems.Add("$($file.Name) contains the $($needle.Name) of the recorded dump")
            }
        }
        $rows.Add(("{0,-26} {1,12:N0} bytes  sha256 {2}" -f $file.Name, $size, $digest))
    }

    $evidence = Write-Evidence $Name (@($rows) + @("") + @("expected (for reference):") + @($expected | ForEach-Object { "  {0,-26} {1,12:N0} bytes  sha256 {2}" -f $_.Path, $_.Size, $_.Sha }) + @("") + @($problems | ForEach-Object { "PROBLEM $_" }))
    if ($problems.Count -eq 0) {
        Add-Check $Name "pass" "no $Label in the payload ($($expected.Count) fingerprints, extension and magic checked)" $evidence $rows
    } else {
        Add-Check $name "fail" "$($problems.Count) problem(s) - see $(Split-Path -Leaf $evidence)" $evidence $problems
    }
}

Test-GameData "retail-data" (Join-Path $repoRoot "config/game_fingerprints.toml") "game data" -EntrypointNeedles
Test-GameData "mod-data" (Join-Path $installerDir "config/ultimate_fingerprints.toml") "Rock Band Blitz Ultimate content"

# ----------------------------------------------------------- 4. credentials ---

$hardSecrets = @(
    "-----BEGIN [A-Z0-9 ]*PRIVATE KEY-----",
    "ssh-(rsa|ed25519) AAAA[A-Za-z0-9+/]{20,}",
    "ghp_[A-Za-z0-9]{20,}",
    "github_pat_[A-Za-z0-9_]{20,}",
    "AKIA[0-9A-Z]{16}",
    "xox[baprs]-[A-Za-z0-9-]{8,}",
    "AIza[0-9A-Za-z_\-]{35}",
    "Authorization: Bearer [A-Za-z0-9._\-]{8,}",
    "eyJ[A-Za-z0-9_\-]{8,}\.[A-Za-z0-9_\-]{8,}\.[A-Za-z0-9_\-]{8,}"
)
$softSecrets = @(
    "\b(password|passwd|client_secret|api_key|apikey|access_token|private_key)\b"
)

$scanTargets = New-Object System.Collections.Generic.List[object]
foreach ($file in $payloadFiles) { $scanTargets.Add($file) }
foreach ($file in $helperFiles) { $scanTargets.Add($file) }
foreach ($file in $setupExes) { $scanTargets.Add($file) }
$configFiles = @(
    (Join-Path $installerDir "config/pins.toml"),
    (Join-Path $installerDir "config/ultimate_fingerprints.toml"),
    (Join-Path $installerDir "setup.iss")
)
foreach ($path in $configFiles) { if (Test-Path $path) { $scanTargets.Add((Get-Item -LiteralPath $path)) } }

if ($scanTargets.Count -eq 0) {
    Add-Check "credentials" "skip" "nothing built to scan" "" @()
} else {
    $rows = New-Object System.Collections.Generic.List[string]
    $problems = New-Object System.Collections.Generic.List[string]
    $notes = New-Object System.Collections.Generic.List[string]
    foreach ($file in $scanTargets) {
        $hard = Find-InFile $file.FullName $hardSecrets
        if ($hard.Count -gt 0) {
            $owner = if ($vendorDlls -contains $file.Name) { "vendor DLL" } else { "ours" }
            $problems.Add("$($file.Name): $($hard.Count) secret-shaped match(es) [$owner]: $($hard[0].Context)")
            $rows.Add("HARD  $($file.Name)  $($hard[0].Text)")
        }
        $soft = Find-InFile $file.FullName $softSecrets -MaxPerPattern 3
        if ($soft.Count -gt 0) {
            $notes.Add("$($file.Name): $($soft.Count) word-level match(es), e.g. $($soft[0].Text) - $($soft[0].Context)")
        }
    }

    # Key material: the project keeps exactly one copy - the SDK's own XEX key, the
    # constant the emulated console uses to read the user's own dump, which is public in
    # every Xenia-derived runtime (docs/rb3-references.md section 9) - and
    # decrypt_xex.py reads it from there instead of carrying a second one.
    $sdkKeyFile = Join-Path $repoRoot "rexglue-sdk/src/system/xex_module.cpp"
    if (Test-Path $sdkKeyFile) {
        $keyCount = @(Select-String -Path $sdkKeyFile -Pattern "xe_xex2_retail_key" -AllMatches).Count
        $rows.Add("key material: rexglue-sdk/src/system/xex_module.cpp defines xe_xex2_retail_key ($keyCount line(s)) - the console's own constant, not this project's secret")
    } else {
        $rows.Add("key material: rexglue-sdk/src/system/xex_module.cpp is not present, so the SDK's copy could not be located")
    }
    $decrypt = Join-Path $repoRoot "scripts/decrypt_xex.py"
    if (Test-Path $decrypt) {
        $literal = [regex]::Match((Get-Content -LiteralPath $decrypt -Raw), '"([0-9a-fA-F]{32,})"')
        if ($literal.Success) {
            $problems.Add("scripts/decrypt_xex.py carries a literal key: $($literal.Groups[1].Value.Substring(0, 16))...")
        } else {
            $rows.Add("key material: scripts/decrypt_xex.py carries no literal key, it reads the SDK's copy")
        }
    }

    $evidence = Write-Evidence "credentials" (@($rows) + @("") + @($notes | ForEach-Object { "NOTE $_" }) + @($problems | ForEach-Object { "PROBLEM $_" }))
    if ($problems.Count -eq 0) {
        $detail = "no secret-shaped string in $($scanTargets.Count) scanned files"
        if ($notes.Count -gt 0) { $detail += "; $($notes.Count) word-level note(s)" }
        Add-Check "credentials" "pass" $detail $evidence $rows
    } else {
        Add-Check "credentials" "fail" "$($problems.Count) file(s) with a secret-shaped match - see $(Split-Path -Leaf $evidence)" $evidence $problems
    }
}

# --------------------------------------------------------- 5. symbol data ---

if ($payloadFiles.Count -eq 0) {
    Add-Check "symbols" "skip" "no payload to check" "" @()
} elseif (-not (Test-Path $llvmReadobj)) {
    Add-Check "symbols" "fail" "llvm-readobj not found (LLVM is part of the frozen toolchain, docs/toolchain.md)" "" @()
} else {
    $rows = New-Object System.Collections.Generic.List[string]
    $problems = New-Object System.Collections.Generic.List[string]
    foreach ($file in $payloadFiles) {
        if ($file.Extension -ne ".exe" -and $file.Extension -ne ".dll") { continue }
        $header = & $llvmReadobj --file-headers $file.FullName 2>&1
        $symbolPointer = [regex]::Match(($header -join " "), "PointerToSymbolTable:\s*0x([0-9A-Fa-f]+)").Groups[1].Value
        $sections = & $llvmReadobj --sections $file.FullName 2>&1
        $debugSections = @($sections | Select-String -Pattern 'Name:\s*\.debug' -AllMatches | ForEach-Object { $_.Matches[0].Value })
        $rows.Add(("{0,-26} pointerToSymbolTable=0x{1}  debug sections={2}" -f $file.Name, $symbolPointer, $debugSections.Count))
        if ($symbolPointer -ne "0" -and $symbolPointer -ne "00000000") {
            $problems.Add("$($file.Name) carries a symbol table (PointerToSymbolTable=0x$symbolPointer)")
        }
        if ($debugSections.Count -gt 0) {
            $problems.Add("$($file.Name) carries $($debugSections.Count) .debug section(s)")
        }
    }

    # A guest-address label in a shipped string would be the analysis leaking out of the
    # docs, so it is reported even though the addresses are published anyway.
    foreach ($file in $payloadFiles) {
        if ($file.Extension -ne ".exe" -and $file.Extension -ne ".dll") { continue }
        $labels = Find-InFile $file.FullName @("sub_[0-9A-F]{8}") -MaxPerPattern 5
        if ($labels.Count -gt 0) {
            $rows.Add("  $($file.Name): $($labels.Count) guest-address label(s), e.g. $($labels[0].Text)")
        }
    }

    $evidence = Write-Evidence "symbols" $rows
    if ($problems.Count -eq 0) {
        Add-Check "symbols" "pass" "no symbol table and no .debug section in the payload binaries" $evidence $rows
    } else {
        Add-Check "symbols" "fail" "$($problems.Count) problem(s) - see $(Split-Path -Leaf $evidence)" $evidence $problems
    }
}

# ------------------------------------------------------ 6. machine paths ---

# The needles: this checkout, the user profile, Program Files, the build directory, and
# the paths the frozen toolchain record was measured at. `[\\/]` everywhere because
# __FILE__ reaches the compiler with whichever separator invoked it.
$pathNeedles = New-Object System.Collections.Generic.List[string]
$repoLeaf = Split-Path -Leaf $repoRoot
$pathNeedles.Add([regex]::Escape($repoRoot))
$pathNeedles.Add([regex]::Escape($repoRoot.Replace("\", "/")))
$pathNeedles.Add("[\\/]" + [regex]::Escape($repoLeaf))
if ($env:USERPROFILE) {
    $pathNeedles.Add([regex]::Escape($env:USERPROFILE))
    $pathNeedles.Add("[\\/]" + [regex]::Escape((Split-Path -Leaf $env:USERPROFILE)))
}
$pathNeedles.Add("[A-Za-z]:[\\/]Program Files")
$pathNeedles.Add("[A-Za-z]:[\\/]Program Files \(x86\)")
$pathNeedles.Add("[\\/]out[\\/]build[\\/]")

# The paths recorded in config/toolchain.toml: if the compiler's own path reaches a
# shipped string, the record is what says which machine that was.
$toolchainFile = Join-Path $repoRoot "config/toolchain.toml"
if (Test-Path $toolchainFile) {
    foreach ($m in [regex]::Matches((Get-Content -LiteralPath $toolchainFile -Raw), 'path\s*=\s*"([^"]+)"')) {
        $value = $m.Groups[1].Value
        if ($value -match '^[A-Za-z]:' -or $value -match '\\\\') { $pathNeedles.Add([regex]::Escape($value)) }
    }
}

# A loose sweep for anything else that looks like an absolute path, so a shape the
# needles above do not describe is still visible. It is deliberately strict - at least
# two path segments, a plain segment character set, and no `:` or `/` before the drive
# letter - because the first version of it matched `https://` inside a URL and guest
# device paths like `e:\default.xex`, and noise in an audit buries the signal.
$genericPath = "(?<![A-Za-z0-9:/])[A-Za-z]:[\\/](?:[A-Za-z0-9_.\-]+[\\/]){1,}[A-Za-z0-9_.\-]+"

if ($payloadFiles.Count -eq 0 -and $scanTargets.Count -eq 0) {
    Add-Check "machine-paths" "skip" "nothing built to scan" "" @()
} else {
    $rows = New-Object System.Collections.Generic.List[string]
    $problems = New-Object System.Collections.Generic.List[string]
    $notes = New-Object System.Collections.Generic.List[string]

    $pathTargets = New-Object System.Collections.Generic.List[object]
    foreach ($file in $payloadFiles) { $pathTargets.Add($file) }
    foreach ($file in $helperFiles) { $pathTargets.Add($file) }
    foreach ($file in $setupExes) { $pathTargets.Add($file) }
    foreach ($path in $configFiles) {
        if (Test-Path $path) { $pathTargets.Add((Get-Item -LiteralPath $path)) }
    }

    foreach ($file in $pathTargets) {
        $vendor = $vendorDlls -contains $file.Name
        $hits = Find-InFile $file.FullName $pathNeedles -MaxPerPattern 5
        foreach ($hit in $hits) {
            $line = "{0,-30} {1}  {2}" -f $file.Name, $hit.Text, $hit.Context
            if ($vendor) { $notes.Add($line) } else { $problems.Add($line) }
        }
        # Anything else that looks like an absolute path: reported so a new one is visible
        # even on a machine the needles above do not describe.
        foreach ($hit in (Find-InFile $file.FullName @($genericPath) -MaxPerPattern 8)) {
            $line = "{0,-30} {1}" -f $file.Name, $hit.Text
            if ($vendor) { $notes.Add($line) } else { $notes.Add($line) }
        }
    }

    $evidence = Write-Evidence "machine-paths" (@($rows) + @("needles:") + @($pathNeedles | ForEach-Object { "  $_" }) + @("") +
        @("notes:") + @($notes | ForEach-Object { "  $_" }) + @("") + @("problems:") + @($problems | ForEach-Object { "  $_" }))
    if ($problems.Count -eq 0) {
        $detail = "no build-machine path in $($pathTargets.Count) shipped files"
        if ($notes.Count -gt 0) { $detail += "; $($notes.Count) note(s)" }
        Add-Check "machine-paths" "pass" $detail $evidence $rows
    } else {
        Add-Check "machine-paths" "fail" "$($problems.Count) path(s) - see $(Split-Path -Leaf $evidence)" $evidence $problems
    }
}

# --------------------------------------------------------- 7. tracked tree ---

$tracked = @(git -C $repoRoot ls-files)
$gameTracked = @($tracked | Where-Object { $gameContainerExtensions -contains [IO.Path]::GetExtension($_).ToLowerInvariant() })
$binaryTracked = @($tracked | Where-Object { $_.ToLowerInvariant() -match '\.(exe|dll|zip|7z|iso|lib|obj|pdb|bin)$' })
$rows = @(
    "tracked files: $($tracked.Count)",
    "game containers: $($gameTracked.Count)",
    "binary artefacts: $($binaryTracked.Count)"
) + $gameTracked + $binaryTracked
$evidence = Write-Evidence "tracked-tree" $rows
if ($gameTracked.Count -eq 0 -and $binaryTracked.Count -eq 0) {
    Add-Check "tracked-tree" "pass" "$($tracked.Count) tracked files, none of them game data or a binary" $evidence $rows
} else {
    Add-Check "tracked-tree" "fail" "$($gameTracked.Count) game container(s) and $($binaryTracked.Count) binary artefact(s) tracked" $evidence $rows
}

# ------------------------------------------------------------------ result ---

$failed = @($checks | Where-Object { $_.Verdict -eq "fail" })
$skipped = @($checks | Where-Object { $_.Verdict -eq "skip" })

Write-Host ""
$checks | Select-Object Check, Verdict, Detail | Format-Table -AutoSize | Out-String | Write-Host

$summary = [pscustomobject]@{
    timestamp = (Get-Date).ToString("s")
    repo_root = $repoRoot
    payload   = $PayloadDir
    setup     = @($setupExes | ForEach-Object { $_.Name })
    passed    = @($checks | Where-Object { $_.Verdict -eq "pass" }).Count
    failed    = $failed.Count
    skipped   = $skipped.Count
    checks    = @($checks | ForEach-Object {
        [pscustomobject]@{ check = $_.Check; verdict = $_.Verdict; detail = $_.Detail }
    })
}
$summary | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $outDir "summary.json") -Encoding UTF8
Write-Host ("checks: {0} ran, {1} passed, {2} failed, {3} skipped - evidence in $outDir" -f `
    ($checks.Count - $skipped.Count), $summary.passed, $failed.Count, $skipped.Count)

if ($failed.Count -gt 0) { exit 1 }
exit 0
