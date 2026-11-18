#!/usr/bin/env python3
"""Replay the partial v0.11.0 checkpoint against an independent SDK checkout.

This does not select the production SDK or certify a complete migrated game.
The production stack remains under patches/rexglue-sdk.
"""
from __future__ import annotations

import argparse
from pathlib import Path

from apply_sdk_patches import ROOT, apply_patch_set

SDK_COMMIT = "a3e1d82678ca20d68d4837a8e9ee4652ff073745"
PATCH_DIRECTORY = ROOT / "patches/rexglue-sdk-v0.11"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk-checkout", required=True, type=Path,
                        help="Independent SDK checkout at the exact v0.11.0 commit")
    parser.add_argument("--apply", action="store_true",
                        help="Apply after full preflight; default only checks")
    parser.add_argument("--windows-compatible", action="store_true",
                        help="Accept CRLF equivalents and preserve unrelated untracked files")
    args = parser.parse_args()
    sdk = args.sdk_checkout.resolve()
    if sdk == (ROOT / "rexglue-sdk").resolve():
        raise SystemExit("Use an independent SDK checkout; the production SDK is preserved")
    apply_patch_set(sdk, PATCH_DIRECTORY, SDK_COMMIT, not args.apply,
                    args.windows_compatible)
    print("Partial v0.11.0 checkpoint only; runtime and delivery gates remain open")


if __name__ == "__main__":
    main()
