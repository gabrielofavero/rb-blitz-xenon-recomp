# Rock Band Blitz: Xenon Recompilation

Welcome! :)

This project statically recompiles the Xbox 360 version of Rock Band Blitz using [ReXGlue](rexglue-sdk). Target platforms are Windows, Linux, macOS, iOS, and Android. Meta Quest 3 support is planned for a later date.

## Enhancements

My goal is to make the game feel native on each platform and add features that make the experience more enjoyable. Long road ahead!

### General

| Feature                             | Type     | Status      | Notes                                                                                                                                                                                                                                                                                                                 |
| ----------------------------------- | -------- | ----------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Custom resolutions                  | Toggle   | Done        | Up to 4K, with support for nonstandard aspect ratios, including ultrawide.                                                                                                                                                                                                                                            |
| Ultimate mod compatibility          | Built-in | Done        | All enhancements from Ultimate are compatible. Usage is recommended, as It adds features such as offline power-ups and new HUD elements; see the [Rock Band Blitz Ultimate repository](https://github.com/ultimate-mods-rb/blitz-ultimate) for the full list. The installers offer Ultimate as an optional download. |
| Main menu shader precompilation     | Built-in | Done        | Precompile shaders to **reduce** stutter when playing for the first time.                                                                                                                                                                                                                                       |
| Multiple game targets               | Toggle   | Done        | Switch between standard Rock Band Blitz, Ultimate, and the XBLA demo.                                                                                                                                                                                                                                                 |
| In-game messages and update checker | Built-in | Done        | The defunct Messages section now displays project announcements and updates.                                                                                                                                                                                                                                          |
| Offline power-ups without Ultimate  | Toggle   | To Do       | Add offline power-ups to the base game; Ultimate already supports them.                                                                                                                                                                                                                                               |
| More frame rate options             | Toggle   | To Do       | Further investigation and testing on more powerful hardware are needed.                                                                                                                                                                                                                                               |
| Portrait mode gameplay              | Toggle   | In Progress | Play in a touch native way by clicking on the lane and swiping to switch lanes. Overlays also supported.                                                                                                                                                                                                              |

### Input types

| Feature                            | Type     | Status | Notes                                                                                                                                    |
| ---------------------------------- | -------- | ------ | ---------------------------------------------------------------------------------------------------------------------------------------- |
| Mouse, keyboard, and touch support | Built-in | Done   | Navigate the interface and play songs.                                                                                                   |
| Custom mappings                    | Toggle   | Done   | Customize multiple inputs for gameplay and interface navigation.                                                                         |
| Custom button layouts              | Toggle   | Done   | Button icons and controller diagrams for keyboard/mouse, Xbox 360/Series, PlayStation 3/4/5, Switch 1/2, Steam Deck, and Meta Quest 2/3. |

### DLC management

| Feature                                                     | Type     | Status | Notes                                                                                                                |
| ----------------------------------------------------------- | -------- | ------ | -------------------------------------------------------------------------------------------------------------------- |
| Download customs from [RhythmVerse](https://rhythmverse.co/) | Toggle   | Done   | Download custom songs through an in-game store.                                                                      |
| Simple DLC directory                                        | Built-in | Done   | Place CON and LIVE DLC packages in one directory, without needing to worry about emulating the Xbox 360 file system. |
| Multithreaded DLC loading                                   | Toggle   | Done   | Speed up DLC loading at startup, especially with 1,000 or more packages.                                             |
| DLC crash fixes                                             | Built-in | Done   | Fix some DLC-related crashes during sorting and song loading.                                                        |
| Load Guard                                                  | Toggle   | Done   | Restart after a failed DLC load or a configurable loading timeout.                                                   |
| "Refresh Songs" main menu button                            | Toggle   | Done   | Refresh your library without restarting the game.                                                                    |
| In-game song manager                                        | Built-in | To Do  | Delete DLC or hide songs from the library and menu music.                                                            |
| Support for other DLC formats                               | Built-in | To Do  | Detect and load more custom song formats beyond CON and LIVE.                                                        |

### Main menu music

| Feature                              | Type   | Status | Notes                                                                  |
| ------------------------------------ | ------ | ------ | ---------------------------------------------------------------------- |
| Library background music             | Toggle | Done   | Play songs from your library on the main menu.                         |
| "Play Current Song" main menu button | Toggle | Done   | Start playing the current menu song.                                   |
| Music player                         | Toggle | Done   | A main menu overlay that lets you browse and control background music. |

### Interface

| Feature                          | Type     | Status      | Notes                                                                              |
| -------------------------------- | -------- | ----------- | ---------------------------------------------------------------------------------- |
| Main menu customization          | Built-in | Done        | Choose which items to show or hide on the main menu.                               |
| Custom in-game settings          | Built-in | Done        | Configure all new toggles in-game.                                                 |
| Achievement overlay              | Built-in | Done        | Achievement notifications and menus use custom styling to match the game's design. |
| Ultimate's "Song Search" overlay | Built-in | Done        | Adapt the keyboard prompt for controllers and other input devices.                 |
| Settings Labels overlay          | Toggle   | Done        | Show an in-game explanation of each setting.                                       |
| Portrait mode interface          | Built-in | In Progress | Rearrange the interface for touch controls and mobile navigation.                  |

## How to run

Builds for the target platforms will be available on the [Releases page](https://github.com/gabrielofavero/rb-blitz-xenon-recomp/releases). Desktop packages ask you to select the game assets during installation or on first launch. Mobile builds works by importing assets on first launch.

You can also build the game from the terminal. See [Build and run](docs/guides/build-and-run.md) for platform instructions, including how to build for mobile with assets already included to skip on-device import. Android and Meta Quest build routes are not yet available.

## How is content handled?

The original Xbox 360 executable and retail game archives are not included in the repository or installers. You provide the game package or its extracted contents; the port runs statically recompiled game code with project patches and hooks, and imports the required assets locally. See [content packaging](docs/guides/distributable.md) for details.

* The Windows installer cover adapts existing, pubicly available, box art.
* New button icons and controller diagrams are adaptations of real human work! The PlayStation 3 glyphs are also a recriation, not the original PS3 assets.
* The app icon is the only fully AI-generated artwork: a recreation of the original 64 × 64 Xbox 360 icon. Monochrome and Apple packaging variants are processed exports of supplied icon artwork; see [app icons](assets/icon/README.md).

Artwork credits: Nicolae (XELU) Berbece and Paul Paun (button and controller layouts); [haaldor](https://github.com/haaldor/Xelu_prompts_SVG) (SVG adaptation); zacksly (Switch 1/2 controllers); Valve and Meta (official device artwork adapted for Steam Deck and Quest). See the [attribution record](assets/controller/Attributions.txt).

## Project status and future support

This project is in active development. My goal is to release version 0.1.0 on all target platforms simultaneously, excluding Meta Quest 3.

The game is already in good shape, especially on Windows. Most critical features are implemented, graphics are close to the original, and my tests have sustained long gameplay sessions without crashes.

The challenge is to make the experience consistent across platforms and find edge cases. I test on real hardware, but those tests cover only my particular PC, iPhone model, and OS versions. Pre-releases will help gather feedback from a wider range of devices. Please report problems on the [Issues page](https://github.com/gabrielofavero/rb-blitz-xenon-recomp/issues), including your hardware, OS, game build, and steps to reproduce them.

Once the game is stable and the initially planned features are implemented, I'll release version 1.0.0 and shift the focus to smaller bug fixes. This is a humongous project for a single dev, so please provide feedback as this is the only way to ensure a fully stable port in the future!

## Was AI used?

Yes. This is an AI-assisted project. I also make changes manually and apply my own software development skills throughout the process.
