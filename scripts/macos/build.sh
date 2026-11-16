#!/bin/sh
# Direct local build, including verified private game inputs and controller art.
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
exec sh "$repo/installer/macos/build.sh" --direct "$@"
