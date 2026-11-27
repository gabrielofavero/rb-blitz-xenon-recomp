# User preferences

- Keep documentation, code comments, and other project text focused on functionality,
  behavior, and technical rationale. Do not quote user prompts, reference prompt
  identifiers such as P1 or P2, attribute changes to owner requests, or mention AI
  involvement unless that information is necessary to explain the functionality.
- Before testing behavior by navigating the game or driving its UI (manually or
  through automation), ask whether the user wants to perform the test themselves.
  Wait for their answer before driving the UI. This does not block independent
  code inspection, builds, or tests that do not navigate the game/UI.
- If the user asks you to work autonomously, drive the game/UI as needed without
  asking about manual testing. Honor that authorization for its stated scope
  until the user changes it.
- Always build before asking the user to test something, unless you are sure the
  current build already contains the most recent change. An unbuilt change means
  the user tests stale binaries. Build command:
  `cmake --build --preset win-amd64-release`.
- For iOS builds, reuse the same build and packaging directories on every run.
  Write IPA artifacts directly to `out/ios/packages/` using the established
  filename for the package mode, overwriting the previous IPA. Do not create
  timestamped, feature-specific, or numbered directories or IPA copies under
  `out/`; keep one current IPA per package mode.
- On Windows, run payload test executables through
  `out/build/win-amd64-release/rb_blitz_windows_test_runner.exe <test.exe> [args...]`
  so crashes go to the agent's console or CTest log without desktop dialogs.
  The CTest registrations for `menu_payload`, `store_payload`, and `payload_overlay`
  already use this launcher.
- Overlays (owner directive, 2026-10-24): drawing directly on the guest's own UI
  (authored DTA, the title's own widgets/labels, records we serve) is always
  authorized. A new host-side overlay over the guest's screen — a panel, box,
  button, or a label/text the guest did not draw — needs the owner's explicit
  authorization first; ask and wait. Exception: a feature that already uses
  overlays keeps them, so the overlay a new item expects there (e.g. a new
  Help & Options setting and its label/caption) needs no new ask. Never use an
  overlay to dodge an inconvenient guest route.

- Settings descriptions: keep authored captions to at most 96 ASCII characters,
  including any added apply/restart suffix. Use one short sentence where possible;
  no manual line breaks. Keep option names concise (28 characters maximum including
  the displayed value). Settings Labels has Normal, Big, and Biggest sizes: measure
  wrapped text with the actual font and keep the caption and background within the
  reserved strip below the six-option list and above the footer icons. Shorten long
  captions instead of covering options or Start/Back hints. Preserve the six-entry
  maximum on every settings page. See `src/ui/help_layout.h`.

# ReXGlue SDK patch changes

Before creating or extending a patch under `patches/rexglue-sdk/`:

- Decide whether a new patch file is necessary. Inspect the existing patch that
  owns the behavior and its later corrections. Prefer updating that patch when
  the change belongs to the same responsibility; use a new file for a distinct
  responsibility and explain the boundary.
- Check whether the change supersedes an existing patch, implementation or API.
  Search current consumers, tests and reachable launch routes before removing
  anything. Fold corrections into the owning patch where practical, retire only
  confirmed obsolete code, regenerate dependent contexts and update the lineage
  in `patches/README.md`. Preserve unrelated changes and verify full-stack replay.
- Define the platform scope before editing. Consider Windows, Linux, macOS and
  iOS, their supported architectures and applicable D3D12/Vulkan/MoltenVK paths.
  State whether the change is shared or specific to a platform, backend, version
  or title variant. Guard specific behavior at the appropriate boundary and
  preserve other configurations, defaults and dependencies. Validate the
  available affected configurations and explicitly record those not tested;
  success on one platform does not establish correctness on the others.
