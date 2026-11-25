# Xbox Dev Mode build plan

**Status: To do, 2026-11-25.** Initial source and platform-documentation review only.
No Xbox target, package, console deployment, or hardware acceptance has been completed.

## 1. Goals and delivery order

Deliver an x64 UWP homebrew build for retail Xbox consoles in Developer Mode.
Start with Xbox Series S/X; assess Xbox One separately after the first hardware results.

1. **Bundled build:** compile the game and runtime on a development PC and package the
   verified game assets with the application. This is the first delivery goal.
2. **First-launch build:** check a documented input folder, prepare the supplied game once,
   and reuse the result on later launches. Investigate literal native-code compilation on
   the console as a separate gate. Precompiled code with first-launch asset importing is
   the practical baseline, but does not count as implementing on-console CPU compilation.

DLC support belongs to both builds. Adding songs must not require rebuilding native code.
Public asset-free packaging and private bundled packaging must have distinct asset audits.

## 2. Existing foundations and gaps

The following findings are from static inspection on 2026-11-25; Xbox behavior is unproven.

| Area | Existing foundation | Remaining work |
| --- | --- | --- |
| Native game code | [SDK project integration](../../../rexglue-sdk/cmake/rexglue_project.cmake) generates and compiles code during the host build. | Separate PC build-time tools from UWP target outputs; add an Xbox toolchain record and preset. |
| Windowing, audio, input | The SDK provides SDL3 UI, audio and input modules. | Upstream SDL3 dropped UWP support; select native providers, a compatibility layer, or a maintained fork, including direct SDL use in the title. |
| D3D12 | [Device provider](../../../rexglue-sdk/src/gpu/xenos/host/d3d12/d3d12_provider.cpp) requests feature level 11.0. | Probe actual capabilities and remove or guard desktop-only API assumptions. |
| Presentation | [D3D12 presenter](../../../rexglue-sdk/src/gpu/xenos/host/d3d12/d3d12_presenter.cpp) creates an HWND swap chain. | Add a UWP surface and swap-chain route, such as CoreWindow. |
| Memory | [Windows memory implementation](../../../rexglue-sdk/src/base/memory_win.cpp) has several FromApp allocation and mapping branches. | Validate guest address-space reservation, alias mappings, protection changes and committed memory on console. |
| Platform services | [Application bootstrap](../../../src/rb_blitz_app.h) configures platform paths and runtime services. | Audit DLL loading, restart, HTTP, image decoding, diagnostics, filesystem calls and auxiliary processes for UWP. |
| Asset setup | [iOS importer](../../../installer/ios/README.md) ships precompiled code and imports verified assets once. | Reuse portable import/session policy with Xbox storage and lifecycle handling; UIKit is platform-specific. |
| DLC | [DLC implementation and contract](../../guides/dlc.md) support recursive flat libraries and structured roots, with in-place STFS mounting. | Adapt accessible roots and transfer/install destinations; validate discovery and playback on Xbox. |

Xbox UWP uses x64. Microsoft documents a foreground memory limit of 5 GB for games
and 1 GB for apps; verify the allocation available to the deployed package outside the debugger.
Feature level 11.0 alone does not prove renderer compatibility or performance.

## 3. Decisions

| ID | Decision |
| --- | --- |
| D1 | Use the retail Dev Mode UWP route. GDK console development is a separate platform route. |
| D2 | Deliver the bundled build before adding first-launch preparation. Use D3D12 for the initial graphics route; preserve other platforms and backend defaults. |
| D3 | Keep packaged assets read-only. Place mutable state in application storage obtained through platform APIs, without hard-coded console paths or drive letters. |
| D4 | Keep literal on-console native compilation distinct from asset extraction, hashing, generated menu preparation and graphics shader compilation. |
| D5 | Begin DLC acceptance with internal application storage. Add external USB libraries only after access and file-API compatibility are demonstrated on hardware. |
| D6 | Preserve existing flat and structured DLC formats and read-only package mounting. Keep saves, download staging and generated data outside external source packages. |
| D7 | Scope SDK changes by responsibility and platform. Follow the patch ownership, lineage and replay requirements in the repository instructions before modifying SDK patches. |

## 4. Tooling prerequisites

- [ ] Verify or install Visual Studio C++ UWP tools, compatible CRT libraries and remote debugging support.
- [ ] Retain Clang 20+ for the current ReXGlue requirement; prove the selected compiler/linker configuration against UWP imports and runtime libraries.
- [ ] Use Windows SDK 22621 or newer, MakeAppx, SignTool, CMake and Ninja.
- [ ] Add the application manifest, package identity, signing procedure, dependency staging and deployment scripts.
- [ ] Provision a retail console in Dev Mode and document Device Portal pairing and deployment.
- [ ] Verify game resource allocation and record the console model, OS, SDK and compiler versions with each result.

Initial inspection found Clang, CMake, Ninja, SDK 26100, MakeAppx and SignTool on the
development machine. UWP C++ workload availability and console connectivity remain unverified.
Do not treat this machine inventory as a reproducible build prerequisite check.

## 5. Storage and DLC contract

Proposed application-relative layout; actual paths must come from platform APIs:

```text
Package/
  Game/                    Bundled variant's verified base assets
  Assets/                  Packaged project resources

LocalState/
  Import/                  User-supplied first-launch input
  Game/                    Importer variant's verified base assets
  DLCs/                    Writable internal library and download destination
  Updates/                 Optional compatible Ultimate content
  Saves/
  Config/
  setup.json               Versioned successful-preparation record

LocalCache/
  shaders/
  generated/
  logs/
```

LocalCache is disposable: clearing it must not remove imported assets, DLC, settings or saves.
Document backup/export because application-local persistent data is tied to the installation.

- [ ] Resolve game, user, cache, update and DLC roots before configuring runtime mounts.
- [ ] Support flat recursive STFS libraries and structured `<title_id>/<content_type>/<package>` roots.
- [ ] Preserve CON, LIVE and PIRS containers; do not require extracting each song.
- [ ] Retain package identity, metadata validation, collision policy and existing enumeration semantics.
- [ ] Provide a documented PC upload path into application storage using Device Portal; report transfer failures and incomplete uploads.
- [ ] Keep download staging outside discovery and atomically publish completed installs.
- [ ] Verify Refresh Songs sees additions and preserves already open package lifetimes.
- [ ] Probe USB enumeration, permissions and compatibility with the scanner's native file APIs; extensionless containers must remain usable.
- [ ] Handle missing or disconnected external storage without losing saves or modifying source packages.

Xbox does not support broadFileSystemAccess or standard folder-picker scenarios.
Do not base setup on either feature. Device Portal upload is the initial transfer candidate;
USB is a later hardware gate, including whether accessible files require import into local storage.

## 6. Implementation checklist and acceptance gates

| Step | Deliverable | Completion evidence |
| --- | --- | --- |
| X0 | Reproducible UWP toolchain and a minimal signed x64 package. | Build/package commands and a console deployment log; capabilities and available memory recorded outside the debugger. |
| X1 | Chosen UWP windowing, controller, audio and lifecycle architecture, including the SDL3 replacement boundary. | A console prototype presents D3D12 frames, reads a controller, plays continuous audio, and survives suspend/resume and reconnect. |
| X2 | Runtime compatibility port: memory mappings, packaged modules, UWP surface/presenter, writable paths, network and restart behavior. | Import/API audit, memory and graphics capability results, focused host tests, SDK patch replay, and desktop regression build. |
| X3 | First goal: bundled game package generated from verified local inputs. | Audited package contents; cold and warm boots, menu navigation, song playback, save/relaunch and compatible Ultimate acceptance on Series hardware. |
| X4 | Internal DLC storage and transfer workflow. | Flat and structured samples enumerate and play; refresh, download staging, duplicate handling and a representative large library pass on hardware. |
| X5 | First-launch asset importer with versioned setup state. | Missing/invalid input, valid import, interruption, insufficient space, warm reuse and application update cases pass without replacing a valid installation on failure. |
| X6 | Optional external USB library support. | Discovery and playback from an accessible USB root, reconnect/removal recovery and large-library measurements; record any copy-to-local requirement. |
| X7 | Literal on-console compilation feasibility spike described in section 7. | Explicit go/no-go result with executable-code, persistence, memory and compile-time evidence; asset importing alone cannot close this step. |
| X8 | Release documentation and platform acceptance matrix. | Reproducible packaging/deployment guide, save backup instructions, cache invalidation rules and per-model results; Xbox One remains unclaimed until independently tested. |

Run X0-X4 before treating the bundled build as delivered. X5 depends on that runtime
foundation. X6 and X7 are separate follow-on investigations; neither blocks the first goal.

For game/UI acceptance, first offer the test to the owner and wait before driving the UI,
unless autonomous testing has been authorized for that scope. Build the latest relevant target
before handing it over. No new host overlay is authorized by this plan; obtain explicit
authorization if a new host overlay is needed, and retain guest-native routes where applicable.

## 7. Literal on-console compilation research

The current build runs code generation and native compilation on the development host.
An Xbox-hosted compiler is a separate implementation with these gates:

- [ ] Prove Xbox support for the required generated-code capability and writable-to-executable transitions using UWP APIs and instruction-cache synchronization.
- [ ] Build a small in-process compilation and linking prototype; identify compiler, headers, runtime libraries and code-generator dependencies that must ship.
- [ ] Resolve generated code imports, relocations, function registration and exception/unwind metadata within the application sandbox.
- [ ] Persist compiled output and reload it after a full process restart, without assuming a DLL written into LocalState can be loaded through normal package-library APIs.
- [ ] Key cached output by input XEX fingerprint, compiler/code-generator version, runtime ABI and target architecture; reject stale or corrupt output.
- [ ] Measure total compiler/runtime memory, package size, storage usage, cold compilation time and warm load time against the deployed console's actual limits.
- [ ] Prove the full supported game can be compiled and reused before adopting the route as a delivery target.

Microsoft documents generated-code support through VirtualProtectFromApp with codeGeneration,
but writable/executable permissions cannot be enabled simultaneously. LoadPackagedLibrary
uses the package dependency graph. These API contracts establish design constraints, not proof
that a complete compiler and persistent loader will work on Xbox.

If the spike fails, record the specific blocker and retain the precompiled importer as a
separate deliverable. Never describe asset preparation or shader caching as CPU recompilation.

## 8. Validation and open questions

Record console model/OS, package mode, SDK patch stack, renderer capabilities, memory peaks,
frame-time distribution, audio continuity, startup times, DLC count and storage route.
Include cold/warm launches, cache deletion, app update, suspension, save recovery and library
refresh. Runtime performance and the full graphics feature set are currently unproven.

Open decisions: the SDL3 replacement strategy; console-supported renderer options;
restart semantics; USB access method; default import transfer format; large-library memory
limits; and whether the native compiler prototype justifies its distribution and maintenance cost.

Capture implementation evidence under ignored `out/xbox-dev-mode/` and reference the exact
logs in step notes. Maintain the Windows release regression build with
`cmake --build --preset win-amd64-release`; that build is not Xbox validation.
Record Linux, macOS, iOS and unaffected graphics routes as tested or explicitly untested
when shared SDK code changes. Update build, toolchain, DLC and distribution guides as their
behavior becomes available, rather than presenting planned commands as working instructions.

## 9. Platform sources

Sources reviewed for the initial assessment on 2026-11-25; recheck during X0/X7:

- [Microsoft: Xbox UWP system resources](https://learn.microsoft.com/en-us/previous-versions/windows/uwp/xbox-apps/system-resource-allocation).
- [Microsoft: UWP development environment](https://learn.microsoft.com/en-us/windows/uwp/xbox-apps/development-environment-setup).
- [DirectX and UWP on Xbox Series X/S](https://walbourn.github.io/directx-and-uwp-on-xbox-series-x-s/), including the newer SDK requirement.
- [SDL3: Windows platform support](https://wiki.libsdl.org/SDL3/README-windows).
- [Microsoft: unsupported Xbox UWP features](https://learn.microsoft.com/en-us/uwp/extension-sdks/uwp-limitations-on-xbox).
- [Microsoft: Device Portal APIs](https://learn.microsoft.com/en-us/windows/uwp/debug-test-perf/device-portal-api-core), including application file uploads.
- [Microsoft: VirtualProtectFromApp](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualprotectfromapp).
- [Microsoft: LoadPackagedLibrary](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-loadpackagedlibrary).
