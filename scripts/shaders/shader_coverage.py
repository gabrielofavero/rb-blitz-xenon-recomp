"""Report development shader discoveries and build a reviewable catalogue candidate.

The development executable appends portable recipes to discoveries.jsonl while
playing. This tool revalidates every record with the SDK hash implementation and
merges it with the current preparation manifest. Missing shader source locators
block promotion; guest shader bytes and native driver blobs are never copied.
"""
import argparse
import json
from pathlib import Path
import sys

import shader_pipeline_catalogue as pc

ROOT = Path(__file__).resolve().parents[2]
PATHS = {f"{backend}/{kind}": spec for backend, spec in pc.BACKENDS.items()
         for kind in spec["apis"].values()}


def load_discoveries(journal, hasher):
    records, damaged = [], []
    for number, line in enumerate(pc.read_bounded(journal).decode("utf-8").splitlines(), 1):
        try:
            record = json.loads(line)
        except json.JSONDecodeError:
            # An interrupted append is preserved by the recorder. Later lines
            # remain usable, and the report makes the damaged evidence explicit.
            damaged.append(number)
            continue
        if record.get("schema") != 1 or record.get("title_id") != "5841122D":
            raise ValueError(f"line {number}: unsupported discovery schema/title")
        spec = PATHS.get(record.get("path"))
        if not spec or record.get("version") != spec["version"]:
            raise ValueError(f"line {number}: unsupported rendering path/storage version")
        raw = bytes.fromhex(record["raw"])
        if len(raw) != spec["description_size"]:
            raise ValueError(f"line {number}: wrong description size")
        records.append((number, record, raw))
    hashes = hasher([raw for _, _, raw in records]) if records else []
    if len(hashes) != len(records):
        raise ValueError("hash helper returned an incomplete result")
    unique = {}
    for (number, record, raw), digest in zip(records, hashes):
        if digest != record["hash"]:
            raise ValueError(f"line {number}: description hash mismatch")
        key = (record["path"], digest)
        if key in unique and unique[key] != raw:
            raise ValueError(f"line {number}: description hash collision")
        unique[key] = raw
    return unique, damaged


def merge(manifest, discoveries):
    candidate = json.loads(json.dumps(manifest))
    if candidate["schema"] != 1 or candidate["title_id"] != "5841122D":
        raise ValueError("unsupported preparation manifest")
    sources = {(source["stage"], source["hash"]) for source in candidate["sources"]}
    missing, additions = set(), {}
    for (path, digest), raw in sorted(discoveries.items()):
        backend = path.split("/")[0]
        description = pc.describe_record(backend, digest, raw)
        recipes = candidate["paths"][path]
        by_hash = {entry["hash"]: entry["raw"] for entry in recipes}
        if digest in by_hash:
            if by_hash[digest] != raw.hex():
                raise ValueError(f"{path}/{digest}: catalogue hash collision")
            continue
        for stage, identity in (("vertex", description.vertex_hash), ("pixel", description.pixel_hash)):
            if identity and (stage, identity) not in sources:
                missing.add((stage, identity))
        recipes.append({"hash": digest, "raw": raw.hex()})
        additions[path] = additions.get(path, 0) + 1
    for recipes in candidate["paths"].values():
        recipes.sort(key=lambda entry: entry["hash"])
    return candidate, additions, sorted(missing)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--journal", type=Path, default=ROOT / "out/shader-coverage/discoveries.jsonl")
    parser.add_argument("--manifest", type=Path, default=ROOT / "config/graphics_preparation.json")
    parser.add_argument("--hash-helper", type=Path,
                        default=ROOT / "out/build/win-amd64-release/rb_blitz_shader_microcode_hash.exe")
    parser.add_argument("--candidate-out", type=Path, help="write a new manifest; refuses to overwrite")
    args = parser.parse_args(argv)
    try:
        discoveries, damaged = load_discoveries(args.journal,
            lambda codes: pc.sdk_hashes(codes, args.hash_helper.resolve()))
        candidate, additions, missing = merge(pc.read_json(args.manifest), discoveries)
        print(json.dumps({"unique_discoveries": len(discoveries), "new_recipes": additions,
                          "unresolved_sources": [f"{s}:{h}" for s, h in missing],
                          "damaged_journal_lines": damaged}, indent=2))
        if missing:
            print("Resolve these shader source locators before promoting the candidate.", file=sys.stderr)
            return 2
        if args.candidate_out:
            args.candidate_out.parent.mkdir(parents=True, exist_ok=True)
            with args.candidate_out.open("x", encoding="utf-8", newline="\n") as stream:
                json.dump(candidate, stream, indent=2)
                stream.write("\n")
            print(f"Candidate: {args.candidate_out}")
        return 0
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f"shader coverage: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
