#!/bin/sh
# Separate asset-free IPA; the existing build.sh keeps embedding private assets.
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
exec sh "$repo/installer/ios/build.sh" --importer "$@"
