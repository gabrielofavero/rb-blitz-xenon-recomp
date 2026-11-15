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

for command in cmake ninja python3 git xcrun pkgutil lipo otool sips iconutil codesign install_name_tool strip; do
  command -v "$command" >/dev/null 2>&1 || {
    echo "Missing required build tool: $command" >&2
    exit 1
  }
done
python3 -c 'import sys; sys.exit(0 if sys.version_info >= (3, 11) else "Python 3.11+ is required; put it first on PATH")'
# Respect DEVELOPER_DIR, including a side-by-side Xcode selected for this build.
# /usr/bin/clang may otherwise select obsolete Command Line Tools instead.
clang=$(xcrun --find clang)
clangxx=$(xcrun --find clang++)
sdk=$(xcrun --show-sdk-path)
if ! test -f game/default.xex; then
  echo 'Missing supported game/default.xex; extract your own game package before building' >&2
  exit 1
fi
if ! test -f rexglue-sdk/CMakeLists.txt; then
  git submodule update --init --recursive
fi
python3 scripts/setup/apply_sdk_patches.py
# Keep fetched MoltenVK dependencies within the path-remapped checkout.
export CPM_SOURCE_CACHE="$repo/out/macos/dependencies"
echo "Host: $(sw_vers -productVersion) $(uname -m)"
echo "Apple SDK: $(xcrun --show-sdk-version)"
echo "Clang: $(xcrun clang --version | head -n 1)"
if command -v xcodebuild >/dev/null 2>&1; then xcodebuild -version || true; fi

cmake --preset "$preset" -DCMAKE_C_COMPILER="$clang" -DCMAKE_CXX_COMPILER="$clangxx" \
  -DCMAKE_OBJC_COMPILER="$clang" -DCMAKE_OBJCXX_COMPILER="$clangxx" -DCMAKE_OSX_SYSROOT="$sdk"
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
cmake --build --preset "$preset" --target rb_blitz rb_blitz_macos_launcher rb_blitz_controller_schemes
python3 installer/macos/package.py --arch "$arch" --build-dir "out/build/$preset" "$@"
