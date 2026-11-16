#!/bin/sh
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
exec sh "$repo/installer/ios/build.sh" "$@"
