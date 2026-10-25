# SPDX-License-Identifier: GPL-2.0-only
"""Extract the actual generated retail routines for the offline sort regression.

Avoid a hand-translated copy that could diverge from the game's instructions.
Only config lookup and non-command node evaluation are supplied by the fixture.
"""
import argparse
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--generated", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    wanted = {"sub_821D16A8", "sub_821D0F30", "sub_821C9550", "sub_823FAE00",
              "sub_823F9630", "__savegprlr_25", "__restgprlr_25"}
    found = {}
    for path in sorted(args.generated.glob("rb_blitz_recomp.*.cpp")):
        source = path.read_text(encoding="utf-8")
        for name in sorted(wanted - found.keys()):
            signature = f"DEFINE_REX_FUNC({name})"
            start = source.find(signature)
            if start < 0:
                continue
            end = source.find("\nDEFINE_REX_FUNC(", start + len(signature))
            body = source[start:end if end >= 0 else len(source)]
            if name == "sub_821D0F30":
                # The real project hook supplies the public entry; keep its original callable.
                body = body.replace(signature, "REX_EXTERN(__imp__sub_821D0F30)", 1)
            found[name] = body
        if found.keys() == wanted:
            break
    if missing := wanted - found.keys():
        raise SystemExit(f"Missing generated routines: {sorted(missing)}")
    args.output.write_text("// Generated native sort regression fixture.\n" +
                           "\n".join(found[name] for name in sorted(found)), encoding="utf-8")


if __name__ == "__main__":
    main()
