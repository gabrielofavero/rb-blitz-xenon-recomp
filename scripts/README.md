# Scripts

Run scripts from the repository root unless a script says otherwise. Paths in the
documentation and build configuration use these directories:

| Directory | Purpose |
| --- | --- |
| `acceptance/` | End-to-end game acceptance runs. These launch and drive the game. |
| `assets/` | Game archive, scene, texture, and icon tools. |
| `controllers/` | Controller scheme assets, templates, and pad art generators. |
| `diagnostics/` | UI observation, probes, captures, and process diagnostics. Some launch and drive the game. |
| `setup/` | Checkout repair, SDK patch application, and distributable audit. |
| `shaders/` | Shader discovery, catalogue, cache, and coverage tools. |
| `xex/` | XEX header inspection and image decryption tools. |

For example, regenerate the Steam Deck pad diagrams with
`python scripts/controllers/generate_steam_deck_pads.py`.
