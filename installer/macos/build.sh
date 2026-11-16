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
direct=false
source_path=
ultimate=false
ultimate_source=
output=out/macos/packages
mode=
while [ "$#" -gt 0 ]; do
  case "$1" in
    --direct) direct=true; shift ;;
    --ultimate) ultimate=true; shift ;;
    --stage-only|--development) mode=$1; shift ;;
    --arch|--source|--output|--ultimate-source)
      test "$#" -ge 2 || { echo "Missing value for $1" >&2; exit 2; }
      case "$1" in
        --arch) arch=$2 ;;
        --source) source_path=$2 ;;
        --output) output=$2 ;;
        --ultimate-source) ultimate_source=$2; ultimate=true ;;
      esac
      shift 2 ;;
    --help|-h)
      echo 'Usage: sh installer/macos/build.sh [--direct | --stage-only | --development] [--source FILE_OR_FOLDER] [--ultimate | --ultimate-source ZIP_OR_FOLDER] [--arch arm64|x86_64] [--output DIR]'
      exit 0 ;;
    *) echo "Unknown argument: $1" >&2; exit 2 ;;
  esac
done
if [ "$direct" = true ] && [ -n "$mode" ]; then
  echo '--direct cannot be combined with --stage-only or --development' >&2
  exit 2
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
# Prepare/verify private build inputs with the same SDK-free importer as the app.
cmake -S installer/portable -B out/build/macos-import -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER="$clangxx" \
  -DCMAKE_OBJCXX_COMPILER="$clangxx" -DCMAKE_OSX_SYSROOT="$sdk" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 -DCMAKE_OSX_ARCHITECTURES="$arch"
cmake --build out/build/macos-import --target rb_blitz_import
if [ -n "$source_path" ]; then
  out/build/macos-import/rb_blitz_import --fingerprints config/game_fingerprints.toml \
    --destination game --source "$source_path"
else
  out/build/macos-import/rb_blitz_import --fingerprints config/game_fingerprints.toml \
    --destination game --verify
fi
if [ "$ultimate" = true ]; then
  if [ -n "$ultimate_source" ]; then
    out/build/macos-import/rb_blitz_import --fingerprints config/game_fingerprints.toml \
      --destination game --verify --ultimate-source "$ultimate_source"
  else
    out/build/macos-import/rb_blitz_import --fingerprints config/game_fingerprints.toml \
      --destination game --verify --ultimate
  fi
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
  -DCMAKE_OBJC_COMPILER="$clang" -DCMAKE_OBJCXX_COMPILER="$clangxx" -DCMAKE_OSX_SYSROOT="$sdk" \
  -U CPM_DIRECTORY -U CPM_FILE -U CPM_VERSION \
  -DCPM_SOURCE_CACHE="$CPM_SOURCE_CACHE"
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
if [ "$direct" = true ]; then
  python3 scripts/macos/stage_direct.py --build-dir "out/build/$preset"
else
  if [ -n "$mode" ]; then set -- "$mode"; else set --; fi
  python3 installer/macos/package.py --arch "$arch" --build-dir "out/build/$preset" --output "$output" "$@"
fi
