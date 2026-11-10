#!/bin/sh
# Build the native ReXGlue target, then stage the Finder app and optional pkg.
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
if [ "$(uname -s)" != Darwin ]; then
  echo 'macOS is required for the native .app/.pkg build' >&2
  exit 1
fi

arch=$(uname -m)
if [ "$#" -ge 2 ] && [ "$1" = --arch ]; then
  arch=$2
  shift 2
fi
case "$arch" in
  arm64) preset=mac-arm64-release ;;
  x86_64) preset=mac-amd64-release ;;
  *) echo "Unsupported native Mac architecture: $arch" >&2; exit 1 ;;
esac
if [ "$(uname -m)" != "$arch" ]; then
  echo 'Build each architecture on matching native Mac hardware for this route' >&2
  exit 1
fi

for command in cmake ninja python3 xcrun pkgutil lipo otool sips iconutil; do
  command -v "$command" >/dev/null 2>&1 || {
    echo "Missing required build tool: $command" >&2
    exit 1
  }
done
echo "Host: $(sw_vers -productVersion) $(uname -m)"
echo "Apple SDK: $(xcrun --show-sdk-version)"
echo "Clang: $(xcrun clang --version | head -n 1)"
if command -v xcodebuild >/dev/null 2>&1; then xcodebuild -version || true; fi

cmake --preset "$preset"
if [ ! -f generated/default/sources.cmake ]; then
  mkdir -p out/macos
  log=out/macos/codegen-bootstrap.log
  if cmake --build --preset "$preset" --target rb_blitz_codegen >"$log" 2>&1; then
    :
  elif ! grep -Fq 'Recompiled code has just been generated' "$log"; then
    cat "$log" >&2
    exit 1
  fi
  test -f generated/default/sources.cmake || {
    echo 'Code generation did not produce generated/default/sources.cmake' >&2
    exit 1
  }
  cmake --preset "$preset"
fi
cmake --build --preset "$preset"
python3 installer/macos/package.py --arch "$arch" --build-dir "out/build/$preset" "$@"
