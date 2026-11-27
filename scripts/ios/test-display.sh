#!/bin/sh
# Exercise the native startup probe and safe-area fit in a booted iOS simulator.
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
device=${1:-booted}
output="$repo/out/ios-display-tests"
app="$output/DisplayTests.app"
mkdir -p "$app"
sdk=$(xcrun --sdk iphonesimulator --show-sdk-path)
arch=$(uname -m)
xcrun clang++ -std=c++23 -fobjc-arc -isysroot "$sdk" \
  -target "$arch-apple-ios17.0-simulator" -I "$repo/src" \
  "$repo/tests/ios_display_tests.mm" "$repo/src/platform/ios_display.mm" \
  -framework UIKit -framework Foundation -o "$app/DisplayTests"
cat > "$app/Info.plist" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleIdentifier</key><string>io.github.rbblitz.displaytests</string>
<key>CFBundleExecutable</key><string>DisplayTests</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>CFBundleVersion</key><string>1</string>
<key>CFBundleSupportedPlatforms</key><array><string>iPhoneSimulator</string></array>
<key>MinimumOSVersion</key><string>17.0</string>
<key>UIDeviceFamily</key><array><integer>1</integer><integer>2</integer></array>
<key>UILaunchScreen</key><dict/>
<key>UISupportedInterfaceOrientations</key><array><string>UIInterfaceOrientationPortrait</string></array>
</dict></plist>
EOF
xcrun simctl install "$device" "$app"
xcrun simctl launch --terminate-running-process --console "$device" io.github.rbblitz.displaytests
