# App icons

`blitz.png` and `blitz.ico` are the original desktop assets; the mono pair is
the existing alternate treatment. Apple packaging uses the owner's supplied
512px image, preserved as `blitz-apple-source.png` (2026-11-16).

Regenerate the Apple variants on a Mac, from the repository root:

```sh
mkdir -p out/macos/swift-cache
xcrun swift -module-cache-path out/macos/swift-cache scripts/assets/make_apple_icon.swift
```

The script draws the supplied artwork without redrawing the logo or background.
`blitz-macos.png` is a 1024px canvas with a rounded 824px tile, 100px transparent
padding, and a thin border, following the sibling Zombies project's classic
macOS treatment. The packager generates all `.icns` representations from it.
`blitz-apple.png` is an unmasked square 1024px version for future iOS use; it is
upscaled from the supplied source, not new high-resolution artwork. No iOS target
or Icon Composer integration is implied.

Apple's [app icon guidance](https://developer.apple.com/design/human-interface-guidelines/app-icons)
describes square source artwork and system masking for modern asset catalogs.
Keep the padded classic macOS asset separate from the square iOS source.
