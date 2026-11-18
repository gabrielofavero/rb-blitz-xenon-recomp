"""Exercise extracted acceptance log functions without starting the game or its UI."""
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SHELL = shutil.which("powershell.exe") or shutil.which("pwsh")
START = "XMPSetPlaybackController(00000000, 00000001)"
STOP = "XMPSetPlaybackController(00000000, 00000000)"

RUNNER = r'''
param([string]$Source, [string]$Fixture)
$ErrorActionPreference = "Stop"
$tokens = $null
$parseErrors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile(
    $Source, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count) { throw ($parseErrors | Out-String) }
foreach ($name in @("Get-LogMarkerTimes", "Test-LogFatal", "Wait-For-SongEnvelope")) {
    $function = $ast.Find({ param($node)
        $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -eq $name
    }, $true)
    if (-not $function) { throw "Missing production function $name" }
    Invoke-Expression $function.Extent.Text
}
# Import the actual marker expressions; do not execute the script's startup/route.
foreach ($name in @("startMarker", "stopMarker")) {
    $assignment = $ast.Find({ param($node)
        $node -is [System.Management.Automation.Language.AssignmentStatementAst] -and
        $node.Left.Extent.Text -eq ('$' + $name)
    }, $true)
    if (-not $assignment) { throw "Missing production marker $name" }
    Invoke-Expression $assignment.Extent.Text
}
$inputData = [IO.File]::ReadAllText($Fixture) | ConvertFrom-Json
switch ($inputData.action) {
    "markers" {
        $times = @(Get-LogMarkerTimes $inputData.text $startMarker)
        $out = @($times | ForEach-Object { $_.ToString("yyyy-MM-dd HH:mm:ss.fff") })
        ConvertTo-Json -InputObject $out -Compress
    }
    "fatal" {
        $out = @($inputData.records | ForEach-Object { Test-LogFatal $_ })
        ConvertTo-Json -InputObject $out -Compress
    }
    "envelope" {
        $script:fixtureText = $inputData.text
        function Get-LogText([string]$Path) { return $script:fixtureText }
        $notes = New-Object System.Collections.ArrayList
        $first = Wait-For-SongEnvelope -Path "fixture" -Base 0 -MinSec 60 -TimeoutSec 1 `
            -Label "first song" -Notes $notes
        $replay = Wait-For-SongEnvelope -Path "fixture" -Base 2 -MinSec 60 -TimeoutSec 1 `
            -Label "replay" -Notes $notes
        if (-not $first -or -not $replay) { throw "Missing production playback envelope" }
        [ordered]@{
            index = $first.Index; sec = $first.Sec
            start = $first.Start.ToString("yyyy-MM-dd HH:mm:ss.fff")
            stop = $first.Stop.ToString("yyyy-MM-dd HH:mm:ss.fff")
            replay_index = $replay.Index; replay_sec = $replay.Sec
            notes = @($notes.ToArray())
        } | ConvertTo-Json -Depth 8 -Compress
    }
    default { throw "Unknown parser action" }
}
'''


# Extract only parser functions and the verdict assignments/branch that consume
# them. The top-level launch, filesystem fixtures and UI routes are never run.
HARNESS_RUNNER = r'''
param([string]$Source, [string]$Fixture)
$ErrorActionPreference = "Stop"
$tokens = $null
$parseErrors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile(
    $Source, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count) { throw ($parseErrors | Out-String) }
$inputData = [IO.File]::ReadAllText($Fixture) | ConvertFrom-Json
foreach ($name in $inputData.functions) {
    $function = $ast.Find({ param($node)
        $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -eq $name
    }, $true)
    if (-not $function) { throw "Missing production function $name" }
    Invoke-Expression $function.Extent.Text
}
switch ($inputData.action) {
    "consumer" {
        $assignment = $ast.Find({ param($node)
            $node -is [System.Management.Automation.Language.AssignmentStatementAst] -and
            $node.Left.Extent.Text -eq '$row.Fatal'
        }, $true)
        if (-not $assignment) { throw "Missing fatal verdict assignment" }
        $collect = $ast.Find({ param($node)
            $node -is [System.Management.Automation.Language.AssignmentStatementAst] -and
            $node.Left.Extent.Text -eq '$fatals'
        }, $true)
        $out = @(foreach ($record in $inputData.records) {
            $text = $record; $logText = $record; $lines = @($record -split '\r?\n')
            $row = [pscustomobject]@{ Fatal = $false }
            if ($collect) { Invoke-Expression $collect.Extent.Text }
            Invoke-Expression $assignment.Extent.Text
            $row.Fatal
        })
        ConvertTo-Json -InputObject $out -Compress
    }
    "clean" {
        $branch = $ast.Find({ param($node)
            $node -is [System.Management.Automation.Language.IfStatementAst] -and
            $node.Extent.Text -like '*no clean-shutdown marker in the log*'
        }, $true)
        if (-not $branch) { throw "Missing shutdown verdict branch" }
        $out = @(foreach ($record in $inputData.records) {
            $lines = @($record.text -split '\r?\n')
            $row = [pscustomobject]@{ Clean = [bool]$record.processExited }
            $notes = New-Object System.Collections.ArrayList
            Invoke-Expression $branch.Extent.Text
            $row.Clean
        })
        ConvertTo-Json -InputObject $out -Compress
    }
    "show" {
        $out = @($inputData.records | ForEach-Object { Show-Line $_ })
        ConvertTo-Json -InputObject $out -Compress
    }
    "fault" {
        $log = Join-Path (Split-Path $Fixture -Parent) 'isolated game.log'
        $out = @(foreach ($record in $inputData.records) {
            [IO.File]::WriteAllText($log, $record)
            $script:steps = 0
            $script:failures = @()
            Assert-NoFault -Tag 'fixture' 6>$null
            if ($script:steps -ne 1) { throw "Fault assertion was not executed" }
            $script:failures.Count -gt 0
        })
        ConvertTo-Json -InputObject $out -Compress
    }
    "syntax" { 'true' }
    default { throw "Unknown parser action" }
}
'''


@unittest.skipUnless(SHELL, "PowerShell is required")
class AcceptanceSongLogTests(unittest.TestCase):
    def invoke(self, action, source="scripts/acceptance/acceptance_song.ps1",
               runner_text=RUNNER, **data):
        with tempfile.TemporaryDirectory(prefix="song log parser ") as folder:
            folder = Path(folder)
            runner = folder / "parser.ps1"
            fixture = folder / "fixture.json"
            runner.write_text(runner_text, encoding="utf-8")
            fixture.write_text(json.dumps(dict(action=action, **data)), encoding="utf-8")
            result = subprocess.run([
                SHELL, "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
                "-File", str(runner), "-Source",
                str(ROOT / source),
                "-Fixture", str(fixture)], capture_output=True, text=True,
                errors="replace", timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            return json.loads(result.stdout)

    def test_baseline_current_markers_preserve_order_and_stay_on_their_record(self):
        text = "\r\n".join([
            f"[2026-11-17 23:59:30.001] [debug] [krnl] [t516] {START}",
            "2026-11-17 23:59:40.002 D system     t21844 no playback marker",
            START,  # A marker on the next record has no timestamp of its own.
            f"2026-11-17 23:59:50.003 D system     t21844 {START}",
            f"2026-11-18 00:03:00.004 I core       t00001 {START}",
            f"prefix 2026-11-18 00:04:00.000 I core t1 {START}",
            f"2026-11-18 00:05:00.000 Z core t1 {START}",
            f"2026-11-18 00:06:00.000 D core thread1 {START}",
            f"2026-11-18 00:07:00.000 D core t1 {STOP}",
        ])
        self.assertEqual(self.invoke("markers", text=text), [
            "2026-11-17 23:59:30.001", "2026-11-17 23:59:50.003",
            "2026-11-18 00:03:00.004"])

    def test_fatal_severity_supports_both_formats_without_matching_message_letters(self):
        cases = [
            ("[2026-11-17 22:40:10.873] [FATAL] [core] stopped", True),
            ("[2026-11-17 22:40:10.873] [critical] [core] stopped", True),
            ("2026-11-18 04:35:56.931 C core       t21844 crash report", True),
            ("ordinary line\r\n2026-11-18 04:35:56.931 C system t00001 stopped", True),
            ("2026-11-18 04:35:56.931 E core       t21844 read error", False),
            ("2026-11-18 04:35:56.931 I core       t21844 C critical text", False),
            ("2026-11-18 04:35:56.931 D C t21844 ordinary category", False),
            ("prefix 2026-11-18 04:35:56.931 C core t21844 text", False),
            ("2026-11-18 04:35:56.931 C core wrong-thread text", False),
            ("", False),
        ]
        self.assertEqual(self.invoke("fatal", records=[text for text, _ in cases]),
                         [expected for _, expected in cases])

    def test_save_dlc_persistence_actual_fatal_verdicts_accept_both_formats(self):
        records = [
            "[2026-11-17 22:40:10.873] [FATAL] [core] stopped",
            "[2026-11-17 22:40:10.873] [critical] [core] stopped",
            "2026-11-18 04:35:56.931 C system     t21844 ==== rb_blitz crash report",
            "ordinary line\r\n2026-11-18 04:35:56.931 C system t1 stopped",
            "2026-11-18 04:35:56.931 E system t21844 recoverable read error",
            "2026-11-18 04:35:56.931 I system t21844 C critical text",
            "prefix 2026-11-18 04:35:56.931 C system t21844 text",
            "2026-11-18 04:35:56.931 C system wrong-thread text",
            "",
        ]
        for name in ("save", "dlc", "persistence"):
            with self.subTest(harness=name):
                self.assertEqual(self.invoke(
                    "consumer", source=f"scripts/acceptance/acceptance_{name}.ps1",
                    runner_text=HARNESS_RUNNER, functions=["Test-LogFatal"], records=records),
                    [True, True, True, True, False, False, False, False, False])

    def test_persistence_actual_clean_verdict_still_requires_process_exit(self):
        records = [
            {"text": "[2026-11-17 22:40:10.873] [info] [core] [t1] Title terminated",
             "processExited": True},
            {"text": "2026-11-18 04:35:56.931 I app t21844 Title shutdown completed",
             "processExited": True},
            {"text": "2026-11-18 04:35:56.931 I app t21844 Title shutdown completed",
             "processExited": False},
            {"text": "2026-11-18 04:35:56.931 I app t21844 Title shutdown requested",
             "processExited": True},
            {"text": "", "processExited": True},
        ]
        self.assertEqual(self.invoke(
            "clean", source="scripts/acceptance/acceptance_persistence.ps1",
            runner_text=HARNESS_RUNNER, functions=["Test-LogCleanShutdown"], records=records),
            [True, True, False, False, False])

    def test_persistence_formats_both_prefixes_without_changing_message(self):
        records = [
            "[2026-11-17 22:40:10.873] [critical] [core] [t1] stopped [evidence]",
            "2026-11-18 04:35:56.931 C system     t21844 stopped [evidence]",
            "2026-11-18 04:35:56.931 I app        t1 ordinary C text",
            "prefix 2026-11-18 04:35:56.931 C system t1 untouched",
            "2026-11-18 04:35:56.931 C system wrong-thread untouched",
            "raw message",
        ]
        self.assertEqual(self.invoke(
            "show", source="scripts/acceptance/acceptance_persistence.ps1",
            runner_text=HARNESS_RUNNER, functions=["Show-Line"], records=records),
            ["[critical] stopped [evidence]", "[C] stopped [evidence]", "[I] ordinary C text",
             *records[3:]])

    def test_in_song_actual_fault_assertion_rejects_legacy_faults_and_current_critical(self):
        records = [
            "[2026-11-17 22:40:10.873] [error] [core] [t1] access violation",
            "[2026-11-17 22:40:10.873] [error] [core] [t1] Unhandled guest exception",
            "[2026-11-17 22:40:10.873] [FATAL] [core] [t1] stopped",
            "2026-11-18 04:35:56.931 C system t21844 ==== rb_blitz crash report",
            "2026-11-18 04:35:56.931 D system t21844 ordinary C critical text",
            "2026-11-18 04:35:56.931 E system t21844 recoverable read error",
            "",
        ]
        self.assertEqual(self.invoke(
            "fault", source="scripts/acceptance/acceptance_controls_in_song.ps1",
            runner_text=HARNESS_RUNNER,
            functions=["Test-LogFatal", "Test-LogFault", "Assert-NoFault"], records=records),
            [True, True, True, True, False, False, False])

    def test_all_matrix_harnesses_and_master_restart_parse_without_execution(self):
        paths = [f"scripts/acceptance/acceptance_{name}.ps1" for name in (
            "song", "save", "dlc", "persistence", "controls", "controls_in_song", "recovery")]
        paths.append("scripts/diagnostics/probe_master_restart.ps1")
        for source in paths:
            with self.subTest(source=source):
                self.assertTrue(self.invoke("syntax", source=source,
                                            runner_text=HARNESS_RUNNER, functions=[]))

    def test_actual_envelope_skips_preview_and_preserves_replay_and_midnight(self):
        text = "\n".join([
            f"[2026-11-17 23:59:30.000] [debug] [krnl] [t516] {START}",
            f"[2026-11-17 23:59:38.000] [debug] [krnl] [t516] {STOP}",
            f"2026-11-17 23:59:50.000 D system     t21844 {START}",
            f"2026-11-18 00:02:51.000 D system     t21844 {STOP}",
            f"[2026-11-18 00:03:00.000] [debug] [krnl] [t516] {START}",
            f"2026-11-18 00:07:02.000 D system     t21844 {STOP}",
        ])
        actual = self.invoke("envelope", text=text)
        self.assertEqual((actual["index"], actual["sec"]), (2, 181))
        self.assertEqual(actual["start"], "2026-11-17 23:59:50.000")
        self.assertEqual(actual["stop"], "2026-11-18 00:02:51.000")
        self.assertEqual((actual["replay_index"], actual["replay_sec"]), (3, 242))
        self.assertEqual(len(actual["notes"]), 1)
        self.assertIn("playback pair 1 lasts 8s", actual["notes"][0])


if __name__ == "__main__":
    unittest.main()
