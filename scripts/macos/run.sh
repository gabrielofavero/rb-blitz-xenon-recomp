#!/bin/sh
# Launch a current direct build. All arguments pass through to the game.
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
case "$(uname -m)" in
  arm64) preset=mac-arm64-release ;;
  x86_64) preset=mac-amd64-release ;;
  *) echo 'Unsupported Mac architecture' >&2; exit 1 ;;
esac
binary="$repo/out/build/$preset/rb_blitz"
test -x "$binary" || { echo 'Run sh scripts/macos/build.sh first' >&2; exit 1; }
cd "$(dirname -- "$binary")"
exec "$binary" "--game_data_root=$repo/game" "$@"
