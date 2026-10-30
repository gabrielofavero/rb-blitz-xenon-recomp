"""Resolve host code addresses to the recompiled guest function that contains them.

`rb_blitz.exe` ships no symbols, so a raw address (a backtrace frame from the store probe's
fault logger, or a sampled rip) means nothing on its own. The link map does carry every
symbol, so this script rebases an address onto the map's preferred base and prints the
nearest function entry at or below it - which for this project is the generated
`sub_XXXXXXXX` of the guest function that was running.

Usage:
  python scripts/diagnostics/map_lookup.py 0x7ff605bf70dd
  python scripts/diagnostics/map_lookup.py --base 0x7ff6058f0000 0x7ff605bf70dd 0x7ff6075f1de6
  python scripts/diagnostics/map_lookup.py --keep all 0x7ff605bf70dd

The module base is logged by the store probe (`store-probe: fault logger installed; module
base 0x...`) and changes every boot, so always pass `--base` when resolving a logged
backtrace. Without it the preferred base is assumed - correct only for an address taken
straight out of the map.

`--keep sub` (the default) resolves against the generated functions only; `--keep all` also
includes the C++ symbols (our own `rb_blitz::...` functions, static initializers). Import
thunks (`__imp_some_dll_symbol`) and compiler labels (`$...`) are always skipped: they sit
between function entries and would hijack a nearest-below lookup.

The map is written only when the link requests one; the build does (`rb_blitz.map` beside
the executable).
"""
import argparse
import bisect
import re
import sys
from pathlib import Path

MAP = Path("out/build/win-amd64-release/rb_blitz.map")
PREFERRED_BASE = 0x140000000
ENTRY = re.compile(r"^\s*[0-9a-f]{4}:[0-9a-f]{8}\s+(\S+)\s+([0-9a-f]{16})\s")
# A recompiled guest function is emitted as `extern "C" __imp__sub_XXXXXXXX`, so that is the name the
# map carries; the bare `sub_` spelling only appears for a handful of hand-written hooks.
GENERATED = re.compile(r"^(__imp__)?sub_[0-9A-F]{8}$")
# Unwind funclets and static-initializer thunks sit next to (and inside) the functions they belong
# to, so a nearest-below lookup would land on one of them instead of the function.
FUNCLET = re.compile(r"^(\?dtor\$|\?\?__|[?$])")


def load_symbols(keep):
    if not MAP.is_file():
        raise SystemExit(f"{MAP} not found: build the release tree first (it writes the map).")
    symbols = []
    with open(MAP, "r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            match = ENTRY.match(line)
            if not match:
                continue
            name = match.group(1)
            if FUNCLET.match(name):
                continue
            if keep == "sub" and not GENERATED.match(name):
                continue
            if keep == "all" and name.startswith("__imp_"):
                continue
            symbols.append((int(match.group(2), 16), name))
    symbols.sort()
    return symbols


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("addresses", nargs="+", help="host code addresses in hex")
    parser.add_argument("--base", type=lambda text: int(text, 0), default=PREFERRED_BASE,
                        help="the module's load base (logged by the fault logger); default is the map's own")
    parser.add_argument("--keep", choices=("sub", "all"), default="sub",
                        help="resolve against generated functions only (default) or every code symbol")
    args = parser.parse_args(argv)

    symbols = load_symbols(args.keep)
    starts = [entry for entry, _ in symbols]
    for raw in args.addresses:
        address = int(raw, 0)
        mapped = PREFERRED_BASE + (address - args.base)
        index = bisect.bisect_right(starts, mapped) - 1
        if index < 0:
            print(f"{address:#x}: below the first symbol")
            continue
        start, name = symbols[index]
        print(f"{address:#x}: {name} +{mapped - start:#x}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
