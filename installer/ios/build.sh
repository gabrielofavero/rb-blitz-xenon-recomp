#!/bin/sh
# Build native tools, import the owner's asset, cross-compile, and package a private IPA.
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
source_path=
ultimate_source=
output=out/ios/packages
bundle_id=io.github.gabrielofavero.rbblitz
sign_identity=
profile=
while [ "$#" -gt 0 ]; do
  case "$1" in
    --source|--ultimate-source|--output|--bundle-id|--sign-identity|--provisioning-profile)
      test "$#" -ge 2 || { echo "Missing value for $1" >&2; exit 2; }
      case "$1" in
        --source) source_path=$2 ;;
        --ultimate-source) ultimate_source=$2 ;;
        --output) output=$2 ;;
        --bundle-id) bundle_id=$2 ;;
        --sign-identity) sign_identity=$2 ;;
        --provisioning-profile) profile=$2 ;;
      esac
      shift 2 ;;
    --help|-h)
      echo 'Usage: sh scripts/ios/build.sh [--source PACKAGE_OR_FOLDER] [--ultimate-source ZIP_OR_FOLDER] [--output DIR] [--bundle-id ID] [--sign-identity ID --provisioning-profile FILE]'
      echo 'Without signing options, creates an unsigned private IPA for re-signing.'
      echo 'Select full Xcode with DEVELOPER_DIR; requires CMake, Ninja and Python 3.11+.'
      exit 0 ;;
    *) echo "Unknown argument: $1" >&2; exit 2 ;;
  esac
done
test "$(uname -s)" = Darwin || { echo 'Build an IPA on macOS with full Xcode' >&2; exit 1; }
if { [ -n "$sign_identity" ] && [ -z "$profile" ]; } ||
   { [ -z "$sign_identity" ] && [ -n "$profile" ]; }; then
  echo '--sign-identity and --provisioning-profile must be supplied together' >&2
  exit 2
fi
for command in cmake ninja python3 git xcrun codesign security; do
  command -v "$command" >/dev/null 2>&1 || { echo "Missing build tool: $command" >&2; exit 1; }
done
python3 -c 'import sys; sys.exit(0 if sys.version_info >= (3, 11) else "Python 3.11+ is required")'
device_sdk=$(xcrun --sdk iphoneos --show-sdk-path) || {
  echo 'Select full Xcode with DEVELOPER_DIR; Command Line Tools have no iPhoneOS SDK' >&2
  exit 1
}
mac_sdk=$(xcrun --sdk macosx --show-sdk-path)
clang=$(xcrun --find clang)
clangxx=$(xcrun --find clang++)
case "$(uname -m)" in
  arm64) host_preset=mac-arm64-release; host_platform=mac-arm64 ;;
  x86_64) host_preset=mac-amd64-release; host_platform=mac-amd64 ;;
  *) echo 'Unsupported Mac host architecture' >&2; exit 1 ;;
esac

# The importer and build-time generators run on the Mac, never on the device.
cmake -S installer/portable -B out/build/ios-import -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
  -DCMAKE_CXX_COMPILER="$clangxx" -DCMAKE_OBJCXX_COMPILER="$clangxx" \
  -DCMAKE_OSX_SYSROOT="$mac_sdk" -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
  -DCMAKE_OSX_ARCHITECTURES="$(uname -m)"
cmake --build out/build/ios-import --target rb_blitz_import
if [ -n "$source_path" ]; then
  out/build/ios-import/rb_blitz_import --fingerprints config/game_fingerprints.toml \
    --destination game --source "$source_path"
else
  out/build/ios-import/rb_blitz_import --fingerprints config/game_fingerprints.toml \
    --destination game --verify
fi
# Private IPAs include Ultimate. Reuse an installed payload, or obtain the
# pinned upstream release with the existing transactional native importer.
if [ -n "$ultimate_source" ]; then
  out/build/ios-import/rb_blitz_import --fingerprints config/game_fingerprints.toml \
    --destination game --verify --ultimate-source "$ultimate_source"
elif ! test -f game/ultimate/gen/patch_xbox.hdr || ! test -f game/ultimate/gen/patch_xbox_0.ark; then
  out/build/ios-import/rb_blitz_import --fingerprints config/game_fingerprints.toml \
    --destination game --verify --ultimate
fi
if ! test -f rexglue-sdk/CMakeLists.txt; then git submodule update --init --recursive; fi
python3 scripts/setup/apply_sdk_patches.py
# Reuse only source downloads, with separate host and device object trees.
export CPM_SOURCE_CACHE="$repo/out/macos/dependencies"
cmake --preset "$host_preset" -DCMAKE_C_COMPILER="$clang" -DCMAKE_CXX_COMPILER="$clangxx" \
  -DCMAKE_OBJC_COMPILER="$clang" -DCMAKE_OBJCXX_COMPILER="$clangxx" \
  -DCMAKE_OSX_SYSROOT="$mac_sdk" -DCPM_SOURCE_CACHE="$CPM_SOURCE_CACHE"
cmake --build --preset "$host_preset" --target rexglue rb_blitz_fingerprint \
  rb_blitz_toolchain rb_blitz_controller_schemes
if [ ! -f generated/default/sources.cmake ]; then
  mkdir -p out/ios
  if cmake --build --preset "$host_preset" --target rb_blitz_codegen >out/ios/codegen-bootstrap.log 2>&1; then
    :
  elif ! grep -Fq 'Recompiled code has just been generated' out/ios/codegen-bootstrap.log; then
    cat out/ios/codegen-bootstrap.log >&2
    exit 1
  fi
  test -f generated/default/sources.cmake || { echo 'Code generation failed' >&2; exit 1; }
fi
cmake --preset ios-arm64-release -DCMAKE_C_COMPILER="$clang" -DCMAKE_CXX_COMPILER="$clangxx" \
  -DCMAKE_OBJC_COMPILER="$clang" -DCMAKE_OBJCXX_COMPILER="$clangxx" \
  -DCMAKE_OSX_SYSROOT="$device_sdk" -DCPM_SOURCE_CACHE="$CPM_SOURCE_CACHE" \
  -DREXGLUE_HOST_EXECUTABLE="$repo/rexglue-sdk/out/$host_platform/rexglue" \
  -DRBBLITZ_HOST_BUILD_DIR="$repo/out/build/$host_preset"
cmake --build --preset ios-arm64-release --target rb_blitz
if [ -n "$sign_identity" ]; then
  set -- --sign-identity "$sign_identity" --provisioning-profile "$profile"
else
  set --
fi
python3 installer/ios/package.py --build-dir out/build/ios-arm64-release \
  --game-dir game --controller-validator "out/build/$host_preset/rb_blitz_controller_schemes" \
  --output "$output" --bundle-id "$bundle_id" "$@"
