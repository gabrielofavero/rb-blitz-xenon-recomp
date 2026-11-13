# SPDX-License-Identifier: GPL-2.0-only
"""Exercise real process exit, including a stuck static destructor; no game UI."""
import subprocess
import sys
import time

fixture, launcher = sys.argv[1:]
for mode in ("normal", "blocked", "destructor", "repeat"):
    command = ([launcher] if launcher else []) + [fixture, mode]
    started = time.monotonic()
    result = subprocess.run(command, timeout=8, check=False)
    elapsed = time.monotonic() - started
    expected = 7 if mode == "normal" else 0
    assert result.returncode == expected, (mode, result.returncode, expected)
    assert elapsed < 3, (mode, elapsed)
    print(f"{mode}: exit={result.returncode}, {elapsed:.3f}s", flush=True)
