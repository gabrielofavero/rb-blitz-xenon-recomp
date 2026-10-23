# User preferences

- Send an ntfy notification whenever you finish handling a user prompt, immediately
  before the final response. The user's topic is `/DeepSeek`; use
  `https://ntfy.sh/DeepSeek` as the destination. Keep the notification brief and
  never claim a notification was sent when it was not.
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
