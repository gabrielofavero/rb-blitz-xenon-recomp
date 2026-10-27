# User preferences

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
- Overlays (owner directive, 2026-10-24): drawing directly on the guest's own UI
  (authored DTA, the title's own widgets/labels, records we serve) is always
  authorized. A new host-side overlay over the guest's screen — a panel, box,
  button, or a label/text the guest did not draw — needs the owner's explicit
  authorization first; ask and wait. Exception: a feature that already uses
  overlays keeps them, so the overlay a new item expects there (e.g. a new
  Help & Options setting and its label/caption) needs no new ask. Never use an
  overlay to dodge an inconvenient guest route.
