# SPDX-License-Identifier: GPL-2.0-only
"""Extract the retail track acceptance/skip protocol for the regression test."""
import argparse
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--generated", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    wanted = {"sub_82438880", "sub_824390B0", "sub_824A8D90", "sub_824A7280",
              "__savegprlr_28", "__restgprlr_28"}
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
            if name == "sub_82438880":
                body = body.replace(signature, "REX_EXTERN(__imp__sub_82438880)", 1)
            found[name] = body
        if found.keys() == wanted:
            break
    if missing := wanted - found.keys():
        raise SystemExit(f"Missing generated routines: {sorted(missing)}")
    args.output.write_text("// Generated MIDI track regression fixture.\n" +
                          "\n".join(found[name] for name in sorted(found)), encoding="utf-8")


if __name__ == "__main__":
    main()
