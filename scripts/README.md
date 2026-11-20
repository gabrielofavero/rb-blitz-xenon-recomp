# Scripts

Run scripts from the repository root unless a script says otherwise. Start with
the [build and run guide](../docs/guides/build-and-run.md) for platform setup, or
the [development command reference](dev/README.md) for `dev.sh`, `dev.ps1`, build,
launch, mobile preview, and packaging shortcuts. Paths in the documentation and
build configuration use these directories:

| Directory | Purpose |
| --- | --- |
| `acceptance/` | End-to-end game acceptance runs. These launch and drive the game. |
| `assets/` | Game archive, scene, texture, and icon tools. |
| `controllers/` | Controller scheme assets, templates, and pad art generators. |
| `diagnostics/` | UI observation, probes, captures, and process diagnostics. Some launch and drive the game. |
| `dev/` | Shared development commands and Windows, Linux, macOS, and iOS helpers; load the root `dev.sh`. |
| `ios/` | Build either a private bundled-assets IPA or a separate first-launch importer IPA. |
| `macos/` | Direct native build, validated asset staging, and local launch. |
| `setup/` | Checkout repair, SDK patch application, and distributable audit. |
| `shaders/` | Shader discovery, catalogue, cache, and coverage tools. |
| `xex/` | XEX header inspection and image decryption tools. |

For example, regenerate the Steam Deck pad diagrams with
`python scripts/controllers/generate_steam_deck_pads.py`.
Regenerate both Switch Pro generations with
`python scripts/controllers/generate_switch_pro_pads.py`.
Regenerate both Switch Joy-Con generations with
`python scripts/controllers/generate_switch_joycon_pads.py`.
