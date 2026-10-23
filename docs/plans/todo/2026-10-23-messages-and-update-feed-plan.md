# Messages feed and in-game update flow — plan (2026-10-23)

**Status: planning. Nothing implemented, no code touched.** This document is the plan for a
built-in (not toggleable) message feed served by a Cloudflare Worker, plus the
"a newer version exists" flow that replaces the Messages screen's *Help & Options* prompt with
*Update Game*.

The measurements in §2 were taken on 2026-10-23 against the retail image
(`out/_swap/blitz_image.bin`, image base `0x82000000`), the retail inbox record
(`ui/inbox/gen/inbox.dtb`, extracted to `out/_swap/inbox_retail.dtb`) and the retail inbox scene
(`ui/inbox/gen/inbox.milo_xbox`, extracted to `out/_swap/inbox_retail.milo_xbox`). Both extractions
are gitignored; §15 has the commands to regenerate them.

---

## 1. The request, as requirements

| Id | Requirement |
| --- | --- |
| R1 | A Cloudflare Worker exposes one API route returning an object with the latest version and an array of messages. |
| R2 | Each message carries a title, a body, a date and an **icon name** (no hosted image). |
| R3 | The game uses that array to drive its own Messages screen. |
| R4 | The feed is fetched **on boot**. A successful answer is written as JSON to a path the game owns. A failed request keeps the last JSON. With no JSON at all, the screen keeps its current (retail) behaviour. |
| R5 | Built-in feature: no toggle. The existing *Show Messages* row keeps deciding whether the tab exists; that row is already implemented and is not part of this work. |
| R6 | The game compares its own version with the feed's latest version. When they differ, the Messages screen's bottom bar offers `(START) Update Game` **in place of** `(START) Help & Options`. |
| R7 | Pressing it prompts the player to leave the game and opens the updater executable. |
| R8 | When the version is current, that bottom-bar field is hidden (no `(START) …` prompt on that screen). |
| R9 | Scope of this plan: the JSON shape, the icon requirements, the worker in a new `workers/messages/` folder, the game integration, drawing text and icon from the feed, and the update flow. |

Non-goals: a general news/notification system outside the Messages tab; push updates while the game
runs; any host-drawn UI over the game (see the repository's UI directive — every visible element is
guest-rendered DTA); **hosted or base64 images in the feed** (the icon is a name resolved against
the game's own icon set or the local `assets/messages/` folder, §5).

---

## 2. What exists today (measured 2026-10-23)

### 2.1 The Messages screen is retail's inbox, and it is reachable offline

`ui/inbox/gen/inbox.dtb` (10,339 bytes, `out/_swap/inbox_retail.dtb`) declares a `UIScreen`
`inbox_screen` whose panels are `background_panel shell_music_panel menu_cache_panel inbox_panel
error_popup_panel`, with `header_index 3`. `inbox_panel`:

- `{file inbox.milo}`, `{focus songs.lst}`, `read_message_delay 1000`;
- `enter` → `{this get_inbox_provider}` → `build_list`, `{songs.lst set_provider {this
  get_inbox_provider}}`, `{this get_inbox_provider} update_subject_message subject.lbl message.lbl
  {songs.lst selected_pos}`, `update_inline_help`, loading/scroller visibility, and
  `update_participating_user_presence`;
- `on_messages_updated` repeats the same fill (`build_list`, `update_subject_message`, `refresh`);
- `SCROLL_MSG` re-runs `update_subject_message` for the newly selected row;
- `update_inline_help` **clears** the tokens for `kAction_Confirm/Cancel/Start/Option/ShellOption`
  and then sets `kAction_Cancel → inline_help_back` and
  `kAction_Start → inline_help_options` (`inline_help_options` = `HELP & OPTIONS` in the English
  locale, `out/_swap/locale_eng.txt`);
- the panel's `BUTTON_DOWN_MSG` has the `kAction_Start` case (measured in the same dump):
  `{options_screen set back_screen inbox_screen}` + `{ui goto_screen options_screen}`.

So the bottom bar the owner sees **is** the inline help, and the START press is a plain DTA case in
the same record the settings payload already edits (`ConfigureLibraryHeaderBar` /
`RouteLibraryHeaderNavigation` in `src/settings/menu_payload.cpp:100-160`).

The empty state is data, not a label. The locale carries `default_inbox_subject` = "No messages" and
`default_inbox_message` = "Sorry, you don't have any messages at this time."
(`out/_swap/locale_eng.txt`), and the provider synthesizes one message carrying those tokens when the
network/user gate says so. That is exactly the screenshot in the request.

### 2.2 The provider and the message objects (static, read from generated code)

| Fact | Value |
| --- | --- |
| `InboxPanel` RTTI / primary vtable | `0x82808d38` → `0x8203ce4c` |
| `get_inbox_provider` name | `0x8203d274` (panel method table `0x8203d1xx-0x8203d3xx`) |
| `InboxProvider` RTTI / vtables | `0x82809240` → primary `0x82042a4c`, secondary (UIListProvider) `0x820429ec` |
| Provider DTA names | `build_list` `0x82042f04`, `get_header_token` `0x82042ef0`, `get_msg_type` `0x82042ee0`, `update_subject_message` `0x82042ec8` |
| Provider message handler | `sub_822D6598` (`rb_blitz_recomp.46.cpp`, branch per interned name) |
| `build_list` | `sub_822D5928` (`rb_blitz_recomp.17.cpp`) |
| `InboxMessage` RTTI / ctor | `0x828347ac` / `sub_82747578` (`rb_blitz_recomp.37.cpp`) |
| `InboxMessageMgr` RTTI | `0x8283478c` |
| Inbox JSON keys in the image | `messages` `0x8205a20c`, `subject` `0x8205a1d4`, `message` `0x8205a1cc`, `inbox_type` `0x8205a1dc`, `payload` `0x8205a1e8`, `challenge_type` `0x8205a1f0` |
| Inbox endpoint in the image | `remoteconfig/getinboxmessages/` `0x82059f34` (job class `GetInboxJob`, RTTI `0x8280a490`) |

`build_list`'s shape (read from the generated body, names are ours):

1. Frees each element of the provider's vector at `this+52…this+56` (`sub_824AF4E0`) and clears it;
2. if the gate `sub_821CDCF0(*(void**)0x82805C80)` answers **true**, pushes ONE entry holding a
   **trial-upsell** message built from `upsell_title_inbox` / `upsell_text_inbox` (type `10`, `true`,
   `-1`, `-1`);
3. otherwise walks the `InboxMessageMgr` at `provider+48`: `sub_822B8BD8(mgr)` = count,
   `sub_82746950(mgr, index)` = message pointer, and pushes one **16-byte heap entry** per message:
   `{+0 u32 symbol, +4 InboxMessage*, +8 message->+60, +12 u8 message->+138}`;
4. **when the count is 0**, builds the default message from `default_inbox_subject` /
   `default_inbox_message` (type `1`, `false`, `-1`, `-1`), appends it to the manager
   (`sub_82746DA0(mgr, message, 1)`) and pushes the entry `{symbol, message, 1, 0}`. That is the
   "No messages" state in the screenshot, and it is the measured recipe for a **plain text
   message** — which is exactly what our feed needs.

The vector holds pointers to those 16-byte structs, and `build_list`'s own clear loop frees the
structs (it does not free the messages). The entry's `+0` symbol is interned from the pointer at
`0x8280A594`, the same symbol in all three branches.

`InboxMessage`'s constructor is `sub_82747578(this, String r4, int r5, String r6, bool r7, int r8,
int r9)` and stores `r4` at `+44` (subject String), `r5` at `+60`, `r8`/`r9` at `+52`/`+56`, `r6` at
`+64` (body String), `r7` at `+138`, plus four empty vectors at `+72…+100`, `+112` = 0, and two
6-byte buffers at `+139`/`+145`. The trial-upsell entry passes type `10`, `true`, `-1`, `-1`; the
empty-state entry passes `1`, `false`, `-1`, `-1`.

**Unproven:** which `+60` value means "plain message" (the `is_challenge` / `is_store_offer` /
`is_rbw_upsell` / … getters read fields we have not mapped), **how the row picks its icon** (§2.3),
and whether the label setters localize the strings (they draw locale tokens for the default message,
raw titles for store offers through a different provider). These are M2/M3 spike questions, with a
screen capture as the oracle.

### 2.3 The row icon: the game already has a named icon set

`ui/resource/lists/gen/list_inbox.milo_xbox` (441,418 B) is a `UIListDir` (`default`) holding the
row template: a `UIListMesh` named **`icon`** (`icon.mesh`), `subject_line.lbl`, `time.lbl`, the
`slot.mesh`/`slot.mat` highlight chrome, and twelve icon materials — `icon_challenge.mat`,
`icon_friend_popular.mat`, `icon_friends_play.mat`, `icon_misc.mat`, `icon_more_stars.mat`,
`icon_new_release.mat`, `icon_revisit_song.mat`, `icon_store_plays.mat`, `icon_tournament.mat`,
`icon_decrease_rank.mat`, `icon_increase_rank.mat`, `icon_store_rec.mat`.

The bitmaps live in `ui/recommendations/gen/rec_icon.milo_xbox` (420,996 B), the icon proxy: one
512×512 DXT1 mip chain per chunk, mips 512..16 (174,720 bytes), header big-endian at the name's
tail (bpp `8`, mips−1 `5`, 512×512, pitch 256). `scripts/ark_inline_icons.py` dumps them and was
proven exact on 2026-10-23: the one icon that also ships as a file
(`ui/recommendations/img/gen/icon_more_stars_keep.png_xbox`) is **byte-identical** to its inline
copy. §8 has the table with the converted PNGs.

The icons are black-and-white masks (the list tints them). The scene also carries the row's
non-icon textures — `slot.png` and `slot_bar.png` (512×128, embedded), and the inbox scene's
`frame.tex`/`neon.tex` (1024×1024) — which are the row/pane chrome, not icons.

**Unproven:** how the row chooses its icon. The 16-byte entry's `+0` symbol is a constant from
`0x8280A594` in all three `build_list` branches, so the selection is either by the message type
(`+60`, the value the `is_*` getters read) or inside `UIListMesh` itself. M3 answers it.

### 2.4 The version the game would report does not exist yet

- The root `CMakeLists.txt` says `project(rb_blitz LANGUAGES CXX)` — no `VERSION`.
- `rb_blitz.rc` carries the icon only; no `VERSIONINFO`.
- No source in `src/` defines a game version macro.
- The installer has one version, `[installer] version = "0.1.0"` in `installer/config/pins.toml`,
  documented there as "the version of the installer and of the build inside it", and it names the
  setup executable and the payload manifest. That is the natural single source (§3 D4).

### 2.5 The updater exists and is dormant

- The setup executable places `RockBandBlitzUpdater.exe` under
  `%LOCALAPPDATA%\rb_blitz\update\` (`installer/setup.iss` line ~271, `installer/README.md`
  §"The updater executable and its manifest").
- The updater fetches `update.toml` from `[update] manifest_url`
  (`https://github.com/gabrielofavero/rb-blitz-xenon-recomp/releases/latest/download/update.toml`),
  installs the payload the manifest names (`payload_version`, `payload_url`, `payload_sha256`,
  `payload_size`, `payload_commit`), records the new version in the uninstall entry, and launches the
  game again.
- **Nothing publishes `update.toml` today**, and `[payload] version/url/sha256/size` are empty, so
  the updater would 404 and refuse. `installer/build.ps1` already generates
  `installer/out/generated/update.toml` and accepts `-PayloadUrl`/`-PayloadSha256` for exactly this
  case — it is a release-step decision, not new code (D9, Q1).

### 2.6 The bottom bar's target screen

`options_screen` is retail's options root; the merged settings payload routes the main menu's and
the pause menu's *Help & Options* into the merged root, but the messages screen's START still says
`{ui goto_screen options_screen}`. Whether that lands on retail's page or the merged root is not
measured; R8 removes the question by hiding the prompt and no-oping the press (D6).

### 2.7 What is already reusable

| Need | Existing piece |
| --- | --- |
| HTTPS GET, no redirects, bounded body | `src/store/store_http.{h,cpp}` (`Get`, `HttpOptions::max_body_bytes`) |
| JSON | `nlohmann/json` via the SDK (`src/store/rhythmverse_client.cpp` includes it) |
| Host worker thread that never touches guest state | `src/store/album_art_service.cpp`, `store_catalogue.cpp`, `store_search_service.cpp` |
| Atomic file write | `fs::WriteAtomic` (`src/fs/atomic_file.h:107`) |
| Writable root | `runtime->user_data_root()` (`src/hooks/dlc.cpp:520` uses it for `store/downloads.tsv`) |
| Boot install point | `rb_blitz_app.h::OnPostLoadXexImage` (the store installs sit there) |
| Guest record authoring | `src/settings/menu_payload.cpp`, `src/ui/dtb_author.{h,cpp}`, `src/ui/settings_screen.{h,cpp}` (`AddLocaleEntry`/`ReplaceLocaleEntry`) |
| Guest text delivery | `{file_exists "rbbz-…/…"}` answered by `src/hooks/settings_bridge_hooks.cpp` (the store's search/download already use the pattern) |
| Spawn a child | `platform::CreateChildLauncher()` (`src/platform/game_restart.h`) |
| Close the game | `window()->RequestClose()` (`rb_blitz_app.h:344`, `settings_restart` path) |
| Texture encode | `src/assets/texture_codec.h` + WIC (`album_art_service.cpp`), **DXT1 only** — a DXT5 is valid BC3 but draws as noise (measured 2026-10-21) |
| Version pin to read | `installer/config/pins.toml` `[installer] version` |

---

## 3. Decisions

Every decision is citable as `D<n>` from code comments.

| Id | Decision | Why |
| --- | --- | --- |
| D1 | One GET at boot, before the guest exists, on a host worker thread. The fetch runs even when *Show Messages* is off. | The same answer carries the version (R6), and a guest thread must never do network I/O (project rule). |
| D2 | One endpoint, one document: `GET /v1/messages` returns `{schema_version, latest_version, generated_at, messages[]}`. | R1's "one object"; one cache file; one parse. |
| D3 | `schema_version` is an integer, `1` in this plan. A reader that does not know it **refuses** the document and keeps the cache. Unknown extra keys are ignored. | Same rule as `update.toml` (`installer/src/config.h`), so a future revision cannot half-apply. |
| D4 | The game's version is read at configure time from `installer/config/pins.toml` `[installer] version` and compiled in as `RBBLITZ_GAME_VERSION`. Comparison is numeric, component by component. | One source of truth; the installer already calls it "the version of the installer and of the build inside it". Lexical compare would rank `0.10.0` below `0.9.0`. |
| D5 | An update is offered only when `latest_version > RBBLITZ_GAME_VERSION`. Equal → current; a *newer local* build (dev) → no prompt, logged. | Otherwise every dev build would nag. |
| D6 | On the Messages screen only: with an update available, the inline-help `kAction_Start` token becomes `inline_help_update_game` ("UPDATE GAME") and the START case opens our confirmation screen. With no update, the `kAction_Start` prompt is **removed** and the press does nothing. | R6/R8 literally; Help & Options stays reachable from the main menu row and the pause menu. |
| D7 | The update prompt is an authored guest screen appended to the payload record (retail list resource, our rows and locale entries). No host-drawn pixels. | Repository UI directive (`.github/copilot-instructions.md`). |
| D8 | "Update now" writes a `rbbz-messages/update/run` path. The host then spawns `%LOCALAPPDATA%\rb_blitz\update\RockBandBlitzUpdater.exe` and requests the UI close. | Spawning must happen before the close: `ReXApp::OnClosing` calls `_Exit(0)` (`src/platform/game_restart.h:11-18`). |
| D9 | The in-game update installs **only** if the release publishes `update.toml` (and the payload archive it names). Otherwise the row opens the release page instead. **Owner call — Q1.** | The updater fetches a manifest that no release publishes today. |
| D10 | The date is drawn inside the row text (`YYYY-MM-DD  Title`); the detail pane shows the body unchanged. | The scene has no date label and a milo edit is out of scope (M3 keeps scene edits to texture/binding only). |
| D11 | No cache and no network → the host performs **no injection at all**. The retail empty state ("No messages" / "Sorry, you don't have any messages at this time.") draws untouched. | R4's last sentence, with the least risk. |
| D12 | Cache: `<user_data_root>/messages/feed.json`, written atomically only after a fully validated answer; the previous file is replaced on success only. | Same folder family as `<user_data_root>/store/downloads.tsv`; `fs::WriteAtomic` is the house rule. |
| D13 | The message list is delivered by **post-hooking `InboxProvider::build_list` (`sub_822D5928`)** and replacing its 16-byte entries with ours, built through the retail `InboxMessage` constructor. | Smallest faithful change: retail keeps the list, the detail fill, navigation, the header ring and the empty state. Alternatives in §8. |
| D14 | Icons are **names**, never image bytes: a built-in id from the game's own set (§9.1) or a custom file in `assets/messages/`, resolved locally, falling back to `icon_misc`. | R2; the game already ships a named 512×512 DXT1 icon set, and a hosted image would need a download + encode for no benefit. |
| D15 | The API base is a compile-time constant; a `messages_api_url` **host-only diagnostic cvar** overrides it for acceptance runs. The cvar does not appear in any settings surface. | R5 (no toggle) plus the project's harness needs (every host worker has a test seam). |

---

## 4. The API contract

### 4.1 The document

```json
{
  "schema_version": 1,
  "latest_version": "0.2.0",
  "generated_at": "2026-10-23T18:00:00Z",
  "messages": [
    {
      "id": "2026-10-23-live-services",
      "title": "Live services have moved",
      "body": "Rock Central is offline, so these messages come from the project now.\nMore soon.",
      "published": "2026-10-23",
      "icon": "icon_more_stars"
    }
  ]
}
```

### 4.2 Field rules

| Field | Type | Rule | If it breaks the rule |
| --- | --- | --- | --- |
| `schema_version` | integer | Exactly `1` in this plan. | Whole document refused; the cache is kept. |
| `latest_version` | string | `MAJOR.MINOR.PATCH`, digits only, each component ≤ 5 digits. | No update prompt; the messages are still used; one log line. |
| `generated_at` | string | RFC 3339 UTC (`…Z`). Informational — log line and cache provenance. | Logged as unreadable; messages still used. |
| `messages` | array | ≤ 32 entries, **newest first**; the game preserves the order. | Missing/wrong type → the document is refused; an empty array is valid. |
| `messages[].id` | string | `^[a-z0-9][a-z0-9-]{0,63}$` — path-safe, used for the log lines. | That message is dropped, the rest are used. |
| `messages[].title` | string | 1…64 visible characters, no control characters. | Message dropped. |
| `messages[].body` | string | 1…1024 characters; `\n` allowed; no other control characters. | Message dropped. |
| `messages[].published` | string | `YYYY-MM-DD`, a real date. | Message dropped. |
| `messages[].icon` | string, optional | Either a **built-in id** (the scene texture name, `^(icons?)_[a-z0-9_]+$` — §9.1's table) or a **custom file name** from `assets/messages/` (`^[a-z0-9][a-z0-9_-]{0,63}\.(png|jpg|jpeg)$`, case-insensitive, ≤ 1 MiB). | The message is drawn with the default icon (`icon_misc`), one log line. |

Bounds are enforced on both sides: the worker's validation script refuses a bad document before it is
published; the game clamps and drops per the table above (never trusting the server).

### 4.3 Version comparison

Split on `.` into three numeric components (parse misses → malformed), compare `major`, then
`minor`, then `patch`. Result: `kCurrent`, `kUpdateAvailable`, `kLocalNewer`, `kMalformed`
(`kLocalNewer` and `kMalformed` both mean "no prompt").

### 4.4 Transport policy

- `GET`, one attempt per boot, `store::HttpOptions` timeouts (10 s resolve/connect, 15 s send, 30 s
  receive), `max_body_bytes` = 256 KiB.
- Redirects are **not** followed (the store's transport policy). A 3xx is a failure.
- 200 + parseable document = success. Anything else = failure, cache kept, one log line.
- Headers: `content-type: application/json; charset=utf-8`, `cache-control: public, max-age=300`,
  `etag: "<sha256>"`. Conditional GETs are a later nicety, not part of v1.

---

## 5. Icons: the built-in set and `assets/messages/`

There is **no image in the feed**. A message names an icon; the game resolves it from two places and
never fetches anything for it:

| Order | Source | Form |
| --- | --- | --- |
| 1 | `<exe dir>/assets/messages/<file>` (the shipped folder; a `--messages_assets_dir` override for dev/acceptance) | A PNG or JPEG, any size, encoded at boot to **512×512 DXT1 with mips to 16** (the measured built-in shape) and cached at `<user_data_root>/messages/icons/<sha1>.png_xbox`. |
| 2 | The game's own icon set (§9.1's table) | A Tex name from the scenes (`icon_more_stars`, `icon_misc`, …). |
| 3 | The default | `icon_misc` — the neutral in-game message icon. |

The field is one string: a value with an image extension is a **custom file**, anything else is a
**built-in id**. Resolution is per message; the lookup is case-insensitive and refuses any name with
a path separator or `..` (the same rule as the store's path policy).

| Property | Requirement | Why |
| --- | --- | --- |
| Source format (custom) | PNG or JPEG | Decoded by WIC, already used by `album_art_service.cpp`; nothing vendored. |
| Encoded form | 512×512 DXT1, mips to 16 (174,720 B body) | Byte-identical to the game's own icons (proven on `icon_more_stars`, §8), so the row draws it through the same path. |
| Max source size | 1 MiB per file | Bounded decode; the shipped folder is small. |
| Alpha | None | DXT5 does **not** draw in this build (measured 2026-10-21); DXT1 is the only safe format. |
| Tint | The icons are masks | The list tints them; custom art should be black/white like the built-ins. |
| Folder | `assets/messages/` in the checkout, shipped beside the executable | `installer/tools/make_payload.ps1` copies it; `scripts/audit_distributable.ps1` admits it. |

The worker validates the field: a built-in id must be in §9.1's table, a custom name must name a file
that exists in `assets/messages/` at check time (so a typo fails before publication). No image bytes
ever travel through the API, so there is no download, no URL policy and no per-image cache in the
worker.

---

## 6. The worker: `workers/messages/`

### 6.1 Layout

```
workers/messages/
  README.md              the contract, the icon rules, local dev, deploy
  package.json           wrangler + node:test scripts, no runtime dependencies
  wrangler.toml          name, main, [assets] directory = "./public", compatibility_date
  src/index.ts           fetch handler: the two routes + JSON errors
  public/messages.json   the document (the only file a maintainer edits for a new message)
  scripts/check.mjs      validates the document and its icons (runs in `npm test` and before deploy)
  test/index.test.ts     route tests against the handler (node:test, no network)
  .gitignore             node_modules/, .wrangler/
```

### 6.2 Routes

| Route | Answer |
| --- | --- |
| `GET /v1/messages` | `public/messages.json` with `content-type`, `cache-control: public, max-age=300`, `etag` = SHA-256 of the body; `304` on a matching `If-None-Match`. |
| `GET /healthz` | `{"status":"ok","schema_version":1,"icons":["icon_misc", …]}` — the built-in ids, so a maintainer can check a name without reading the game. |
| anything else | `404` with `{"error":"not_found"}`. |

### 6.3 Validation (`scripts/check.mjs`)

- The document parses and satisfies §4.2 (types, bounds, unique ids, newest-first by `published`).
- Every `icon` is either one of the built-in ids (the list is a constant in the script, kept in sync
  with the engine study's table) or a file that exists in `assets/messages/`, with an allowed
  extension and ≤ 1 MiB.
- `latest_version` equals `installer/config/pins.toml`'s `[installer] version` (read from the same
  checkout), so the feed and the installer cannot drift.
- Exit non-zero with one line per problem; wire it into `npm test` and into the deploy notes.

### 6.4 Local development and deploy

- `npx wrangler dev` → `http://127.0.0.1:8787`; the acceptance harness points `messages_api_url`
  there.
- `npx wrangler deploy` **only with the owner's explicit approval** (repository rule). No secrets
  are needed: the worker is read-only and serves repository content.
- Optional custom domain/route is a deployment choice; the game's compiled base URL is updated in the
  same change that first deploys.
- Upgrade path (not v1): move the document to Workers KV/R2 if editing without a deploy becomes
  necessary. The API shape does not change.

---

## 7. The game's host half

New folder `src/messages/`, in the project's three-layer style (SDK-free core, Windows worker,
guest half):

| File | Role |
| --- | --- |
| `messages_api.{h,cpp}` | SDK-free: the URL policy (HTTPS, allowlisted host, `/v1/…`), the JSON parse into `MessageFeed { schema_version, latest_version, generated_at, messages[] }`, per-field validation (§4.2), `CompareVersions`, and the banner text rules. Tests walk the matrix. |
| `messages_cache.{h,cpp}` | SDK-free: `Load(path, MessageFeed*, error)`, `Save(path, body, error)` — writes the **raw validated body** through `fs::WriteAtomic`, after creating the folder. |
| `messages_service.{h,cpp}` | Windows: one worker thread per boot (mirrors `album_art_service.cpp`). `Install(runtime, api_base)`: load the cache synchronously → publish a snapshot; start the fetch; on success validate, publish, write the cache. Exposes `Snapshot()`, `UpdateOffer()`, `StateToken()` for logs/tests. |
| `messages_icons.{h,cpp}` | Windows: resolve a message's `icon` (built-in id or `assets/messages/` file), encode a custom file to DXT1 512×512 with mips (WIC + `texture_codec`), cache it at `<user_data_root>/messages/icons/`, and publish the bytes for the guest seam (§9). |
| `messages_guest.{h,cpp}` | Guest half: installs the `build_list` post-hook and the `rbbz-messages/` bridge answers. |
| `messages_update.{h,cpp}` | Update path: locate the updater, spawn it through `ChildLauncher`, request the close; the state the payload builder reads. |

Boot wiring: `rb_blitz_app.h::OnPostLoadXexImage`, beside `InstallAlbumArt()`/`InstallStoreCatalogue()`
— `rb_blitz::messages::Install(runtime(), kMessagesApiBase)` then `InstallMessagesGuest()`.
The payload builder reads a frozen snapshot, exactly like `MenuConfiguration` does for the store rows.

Cache and logging:

```
<user_data_root>/messages/feed.json               the last good document (raw body)
<user_data_root>/messages/icons/<sha1>.png_xbox    encoded custom icons (DXT1 512×512 + mips)
```

Log lines (prefixed `messages:`): `cached feed <n> message(s), latest <v>` / `fetched <n> message(s),
latest <v>` / `fetch failed (<reason>); keeping <m> cached` / `no feed; the retail inbox is
untouched` / `update <status>: current <v> vs latest <w>` / `icon <id>: built-in <name>` /
`icon <name>: encoded <bytes> B` / `icon <name>: missing; using icon_misc`.

Diagnostic cvars (host-only allowlist, documented in `docs/engine/diagnostics.md`):
`messages_api_url` (override the base URL) and `messages_assets_dir` (point the icon folder at the
checkout for dev/acceptance). Nothing player-facing; no feature toggle.

---

## 8. Delivering the messages into the guest

### M2-A (recommended) — post-hook `InboxProvider::build_list`

Measured shape (§2.2) makes this concrete:

1. Call the original `sub_822D5928(provider_slot0)`.
2. Read one entry of the provider's vector (`provider+52`) as the **template** for the `+0` symbol
   (the constant `0x8280A594`); the `+8`/`+12` values are the empty-state recipe's `1`/`0` (below).
3. Free retail's 16-byte entries the way `build_list` itself does (`sub_824AF4E0` per element) and
   clear the vector (`sub_823A0158`).
4. For each feed message (up to 32), allocate a 16-byte entry and one `InboxMessage` per message for
   the whole boot:
   - `InboxMessage`: allocate 188 bytes from the guest allocator (`sub_823E0C00`, already wrapped by
     `store_fetch.cpp:120`), intern the subject/body strings (`sub_8245C878` is the intern used by
     the store paths) and call `sub_82747578(obj, &subject_string, 1, &body_string, false, -1, -1)`.
     Type `1` + `false` is the **empty-state recipe** retail itself uses for a plain text message
     (§2.2 step 4), so no `is_*` getter has to be guessed; the spike only confirms that the drawn row
     is our text.
   - Entry: `{interned_symbol_at_0x8280A594, message, 1, 0}`.
   - Keep one guest buffer per string for the boot, never reuse a buffer across messages (the
     `store_fetch` lesson: a reused buffer drew the last record's bytes, `read of guest
     0x00000004`).
5. The panel then calls `update_subject_message` itself; nothing else to drive.

Why this route: retail keeps the list resource, the row highlight, the scroller, the header ring
(LT/RT), the detail pane, the inline help and the empty state. The only thing replaced is the data
source.

**M2 acceptance (one boot):** the Messages screen lists `YYYY-MM-DD  Title` rows with our titles, the
detail pane shows the body of the selected row, arrow keys/scroll change the selection and the body
follows, LT/RT still reach Your Songs/Recommended, B returns to the main menu, zero faults in the log.
Controls: a second boot with an empty array → the retail "No messages" state.

**Stop condition:** if two working days do not produce a drawing list, fall back to M2-B.

### M2-B — authored inbox record (fallback)

Rewrite `ui/inbox/gen/inbox.dtb` in the payload: keep the screen and the panels, replace
`inbox_panel`'s provider usage with `{songs.lst set_data (msg_0 msg_1 …)}` and generate a locale entry
per row (`msg_<i>` = the subject line) plus `msg_<i>_body` tokens; a `SELECT_MSG`/`SCROLL_MSG`
rewrite sets `subject.lbl`/`message.lbl` with `set_token`. Everything is guest text. Risk: the retail
`list_inbox.milo_xbox` row label may be a provider slot (`text_token`) rather than the row symbol;
the first spike step is therefore a static `set_data` + locale test.

### M2-C — retail JSON at the job seam (last resort)

`GetInboxJob` already speaks the retail inbox JSON (`messages[]` with `subject`/`message`/
`inbox_type`/`payload`). If the job's response object can be fed from our feed (netcache insert or a
hook on its completion), the retail manager, provider and screen all remain untouched. The request
plumbing (endpoint table at `0x82059fac`, server base, auth) is unmapped; only attempt this if A and B
both fail, and only with a go/no-go measurement first.

---

## 9. The icon in the guest

The row already has an icon slot: `list_inbox.milo_xbox`'s `UIListMesh icon` over `icon.mesh`, with
twelve `icon_*.mat` materials and a 512×512 DXT1 texture per icon (§2.3). The feed's `icon` picks one
of them, or a custom file encoded to the same shape. The **open question is how the row is told
which one** — `build_list` puts a constant symbol in every 16-byte entry, so the choice is either by
the message type (`+60`) or inside `UIListMesh`. M3 is that measurement plus the two bindings below.

### 9.1 The built-in icon set (§2.3's scenes; converted 2026-10-23)

Twelve icons are extracted byte-exactly (`scripts/ark_inline_icons.py`); the converted PNGs are in
`out/_swap/messages-icons/` (gitignored, regenerated by §15). One contact sheet:
`out/_swap/messages-icons/contact-sheet.png`.

| Feed id (Tex name) | Source in the scene | Row material | The image | Converted PNG |
| --- | --- | --- | --- | --- |
| `icon_friend_popular` | `img/icon_friend_popular.png` | `icon_friend_popular.mat` | a star holding a person silhouette, sweeping lines | `out/_swap/messages-icons/icon_friend_popular.png` |
| `icon_friends_play` | `img/icon_friends_play.png` | `icon_friends_play.mat` | two people side by side | `out/_swap/messages-icons/icon_friends_play.png` |
| `icon_misc` | `img/icon_mix.png` | `icon_misc.mat` | a radio tower with radiating lines | `out/_swap/messages-icons/icon_mix.png` |
| `icon_more_stars` | `img/icon_more_stars.png` | `icon_more_stars.mat` | stars in ovals of growing size (also the only icon that ships as a file, `icon_more_stars_keep.png_xbox`) | `out/_swap/messages-icons/icon_more_stars.png` |
| `icon_new_release` | `img/icon_new_release.png` | `icon_new_release.mat` | a vinyl record half out of its sleeve | `out/_swap/messages-icons/icon_new_release.png` |
| `icon_revisit_song` | `img/icon_revisit_song.png` | `icon_revisit_song.mat` | two loop arrows around a hatched diamond | `out/_swap/messages-icons/icon_revisit_song.png` |
| `icon_store_plays` | `img/icon_store_plays.png` | `icon_store_plays.mat` | a tilted music staff with notes | `out/_swap/messages-icons/icon_store_plays.png` |
| `icon_tournament` | `img/icon_tournament.png` | `icon_tournament.mat` | a circle with two people, sunburst rays | `out/_swap/messages-icons/icon_tournament.png` |
| `icons_decrease_rank` | `img/icons_decrease_rank.png` | `icon_decrease_rank.mat` | two arrows pointing down-left | `out/_swap/messages-icons/icons_decrease_rank.png` |
| `icons_increase_rank` | `img/icons_increase_rank.png` | `icon_increase_rank.mat` | two arrows pointing up-right | `out/_swap/messages-icons/icons_increase_rank.png` |
| `icons_store_rec` | `img/icons_store_rec.png` | `icon_store_rec.mat` | a globe with networked avatars radiating upward | `out/_swap/messages-icons/icons_store_rec.png` |
| `icons_unplayed_songs` | `img/icons_unplayed_songs.png` | *(no row material)* | a crate with a lattice front | `out/_swap/messages-icons/icons_unplayed_songs.png` |
| `icon_challenge` | `img/icon_challenge.png` | `icon_challenge.mat` | — | **no bitmap in the main ark** (declared by both scenes; unproven where it comes from) |

For context, the row's **non-icon** textures: `slot.png` and `slot_bar.png` (512×128, embedded in
`list_inbox`) are the row chrome; `frame.tex`/`neon.tex` (1024×1024, embedded in `inbox.milo`) are the
message pane's frame and glow. They are not icon candidates.

### 9.2 M3-A — built-in ids: find the row's icon choice

Spike, in this order:

1. Boot with the M2-A injection and capture the Messages screen; read which icon the injected rows
draw (the empty state draws one too, so one row is enough).
2. If the icon follows `+60` (the type), map the values to the `icon_*.mat` names by trying the
   built-in ids as types; if it follows the entry's `+0` symbol, feed that symbol from the message's
   icon id (the intern already happens at `0x8280A594`'s call).
3. If neither drives it (the icon is fixed by `UIListMesh`/`default_mat`), record the limit: the
   feed's **first** message icon applies to the whole list, per-message built-in icons are not
   available without a widget edit.

Success = a capture where two injected messages draw two different built-in icons, zero faults.

### 9.3 M3-B — a custom file

Encode `assets/messages/<file>` at boot with `texture_codec` (512×512 DXT1, mips to 16, the measured
built-in shape) and hand the bytes to the guest:

- First attempt: `{new Tex <name>}` + `{<name> set_file "<path>"}` in the inbox record, with the file
  served from `<user_data_root>/messages/icons/` through a payload-overlay read side (an
  `AddSettingsSide`-style call in `src/hooks/overlay_merge.h`, made before `Initialize()`).
- Second attempt: the album-art netcache seam (`sub_82469C28(manager, path, client, 1)`), whose
  completion `sub_82445CD8` writes into `client+100`'s `RndTex`; a synthetic client needs `+92` to be
  the manager's loaded-file container.

Success = the custom icon draws in the row (or the feed's icon slot) with zero faults. If the row
cannot take a per-row texture, the custom file applies to the feed (`icon` of the newest message) and
the limit is recorded.

### 9.4 M3-C — the built-in default only (documented fallback)

No custom files: the feed's `icon` selects a built-in id (or `icon_misc`), with the per-row limit from
§9.2 if it applies. `assets/messages/` is still validated by the worker (so a feed carrying a custom
name fails before publication), and the limitation is written in
`docs/reference/known-issues.md`. This is the release-safe path if the two spikes run out of time.

Budget: one 174,752-byte `.png_xbox` per distinct icon (the built-ins already live in the ark; a
custom file adds one cache file per icon), published once per boot.

---

## 10. The update flow

### 10.1 Version

- `installer/config/pins.toml` `[installer] version` is read by the root `CMakeLists.txt` at configure
  time (a small `file(READ)` + `string(REGEX MATCH)`; configure fails if the key is missing) and
  becomes `target_compile_definitions(rb_blitz PRIVATE RBBLITZ_GAME_VERSION="…")`.
- Optional but recommended: a `VERSIONINFO` in `rb_blitz.rc` built from the same value (CMake
  `set_source_files_properties(rb_blitz.rc PROPERTIES COMPILE_DEFINITIONS …)`), so Explorer and
  support see the number.
- `messages_service` computes `UpdateOffer` at boot from `RBBLITZ_GAME_VERSION` and the feed (D4/D5);
  it is frozen into the payload configuration exactly like `MenuConfiguration` freezes the store rows.

### 10.2 The inbox record edits (in `menu_payload.cpp`, beside the header edits)

Only when a feed was loaded **and** an update is available:

1. In `update_inline_help` (both the `enter` copy and the method): `kAction_Start` →
   `inline_help_update_game`, a new locale entry `UPDATE GAME` (`AddLocaleEntry`).
2. In `inbox_panel.BUTTON_DOWN_MSG`'s `kAction_Start` case: replace the two `options_screen`
   statements with
   `{if_else {file_exists "rbbz-messages/update/available"} {ui goto_screen messages_update_screen} {do}}`.
   (A `{do}` with no statement is the empty branch; if the engine refuses it, use a harmless
   `{set {var first_screen} {var first_screen}}` — M4 verifies on screen.)
3. Append our `UIScreen messages_update_screen` (panels: `background_panel`, `shell_music_panel`,
   `inbox_panel` as the focus target's back target, our panel on `settings_menu.milo`'s list resource
   as `settings_restart_screen` does), with the rows `Update now` / `Later` and the captions
   `UPDATE GAME`, `Leaves the game and opens the updater.`, `B` back to `inbox_screen`.

When no update is available: the `kAction_Start` **prompt is removed** (delete the
`set_action_token kAction_Start …` statement) and the press case is rewritten to the empty branch.
This is R8: no `(START) …` field on that screen.

### 10.3 The confirmation screen

Text (guest-drawn, authored): title `UPDATE GAME`, body `Version <latest> is available. The game
will close and the updater will start.` Rows: `Update now`, `Later`. `Later` returns to
`inbox_screen`. `Update now` writes `rbbz-messages/update/run/0` through the settings bridge's
`write_file`, which the host answers by spawning the updater (D8).

Failure answers (all guest text): `The updater is not installed.` when the exe is missing;
`The updater could not start.` when `CreateProcess` fails; both leave the player on the screen.

### 10.4 The host side

`src/messages/messages_update.{h,cpp}`:

- Path resolution: `%LOCALAPPDATA%\rb_blitz\update\RockBandBlitzUpdater.exe`
  (`SHGetKnownFolderPath(FOLDERID_LocalAppData)` or `LOCALAPPDATA`), with a comment naming
  `installer/config/pins.toml` `[update] dir_name` and `installer/setup.iss`'s placement. A
  diagnostic override (`messages_updater`) exists for acceptance runs.
- Spawn with `platform::CreateChildLauncher()` (no shell, quoted argv), then
  `app_context().CallInUIThreadDeferred([this]{ window()->RequestClose(); })` — the same shape as the
  settings restart handler (`rb_blitz_app.h:320-346`).
- **Race to measure (M5):** the updater starts while the game is still exiting. If the file
  replacement fails, add a relay: the game spawns `rb_blitz_setup_helper.exe` with a new
  `wait-and-run --pid <game pid> --exe <updater>` command (helper already ships in the install
  folder); it waits for the process to exit and then starts the updater. Measure the direct spawn
  first; only add the helper command if the direct one races.
- If the updater is absent (portable install, an older setup, or an elevated install whose
  `{localappdata}` is the admin's — a known installer quirk), log it and answer the guest with the
  "not installed" path. D9's fallback (open the release page) is the owner's call.

### 10.5 Release pipeline (D9)

For `Update Game` to install anything, a release must publish:

1. `update.toml` (generated by `installer/tools/embed_config.cpp`, `installer/build.ps1` writes it to
   `installer/out/generated/update.toml`), at the asset name the updater's compiled
   `manifest_url` asks for (`releases/latest/download/update.toml`);
2. the payload archive the manifest names (`payload_url`, `payload_sha256`, `payload_size`,
   `payload_version`, `payload_commit` — `build.ps1 -PayloadUrl/-PayloadSha256` fills them without
   editing pins).

Until then the flow can only *tell* the player an update exists. Q1 records the decision.

---

## 11. Phases and prompts

Each prompt is self-contained: goal, deliverable, evidence. The order lets the worker and the host
half land before the guest spikes decide their seams.

| Id | Prompt | Depends on |
| --- | --- | --- |
| M0 | Worker: the document, the routes, the validation script, the tests, the README. | — |
| M1 | Host half: `messages_api` + `messages_cache` + `messages_service` + boot wiring + version macro; a boot with the feed served locally logs the fetched count and version. | M0 |
| M2 | Guest spike + implementation for the **text** (M2-A first; fallbacks B/C). Acceptance: the list and the detail pane draw our messages; the empty array draws the retail empty state. | M1 |
| M3 | Guest spike + implementation for the **icon** (§9: built-in id selection first, then a custom file). Acceptance: two messages draw two different built-in icons, or a custom icon draws, or the documented fallback. | M2 |
| M4 | Update plumbing: version compare into `MenuConfiguration`, the two inbox record edits, the authored confirmation screen, the locale entries. Acceptance: a feed with a higher version shows `UPDATE GAME` and the screen; a current feed shows no START prompt. | M2 (M3 not required) |
| M5 | The press: the bridge write, `messages_update` spawn + close, the updater-missing path, the race measurement (and the helper relay only if needed). Acceptance: a stand-in updater records its start after the game exits. | M4 |
| M6 | Custom icons end-to-end: the `assets/messages/` scan, the DXT1 encode/cache pipeline, the serve seam, the fallback to `icon_misc`, the payload/audit/install changes that ship the folder. | M3, M1 |
| M7 | Acceptance harness `scripts/acceptance_messages.ps1`: a local server, the five boots (feed / cache-only / no-feed / update / current), the icon legs, OCR assertions, the stand-in updater. | M2, M4, M5 |
| M8 | Docs + review: the engine study, diagnostics, known-issues, the index rows, `check_docs.py` clean. | M7 |

---

## 12. Tests and acceptance

### Unit (ctest, SDK-free)

| Suite | Pins |
| --- | --- |
| `messages_api` | The parse matrix (missing keys, wrong types, oversize title/body, bad date, duplicate/unsafe id, unknown `schema_version`, 33 messages), the version compare (`0.10.0 > 0.9.0`, malformed, local-newer), the **icon** field (built-in id, custom file name, path separators/`..` refused, unknown id → default), ordering preservation, the "drop one message, keep the rest" rule. |
| `messages_cache` | Atomic write, corrupt/partial file refused, folder creation, a failed save leaves the previous file intact. |
| `messages_icons` | The `assets/messages/` scan (allowed extensions, 1 MiB bound, case-insensitive names), the encode shape (512×512 DXT1, 6 mips, 174,752 B) and the cache key. |
| `messages_payload` | The inbox record with and without an update: `inline_help_update_game` present/absent, the START case rewritten, the appended screen and locale entries, and the record still parses. |
| `messages_guest` | Whatever is pure in the guest half (entry layout arithmetic, the string buffer bookkeeping); the hook itself is boot-proven. |

### Live (`scripts/acceptance_messages.ps1`)

A local HTTP server serves a fixture document (or the worker's `public/` via `npx wrangler dev`);
the boot passes `--messages_api_url=http://127.0.0.1:<port>/` and a scratch `--user_data_root`:

1. **Feed leg** — boot, PLAY → LT/RT to MESSAGES, OCR the first row's `YYYY-MM-DD  Title`, arrow down,
   OCR the second body; assert `messages: fetched` and zero faults.
2. **Icon leg** — a feed with two different built-in ids: OCR-free screenshot check that the two rows
   carry two different icons (the capture is compared against §9.1's converted PNGs, or read by eye).
3. **Cache leg** — stop the server, boot again with the same user root; the same rows draw and the
   log says `keeping N cached`.
4. **Cold leg** — a fresh user root, no server; the retail `No messages` / `Sorry, you don't have any
   messages at this time.` draw, and the START prompt is absent.
5. **Update leg** — a feed whose `latest_version` is the game version + 1: OCR `UPDATE GAME` in the
   bottom bar, press START, OCR the confirmation, press `Later` (back to the inbox). Then press
   `Update now` with `--messages_updater=<stand-in>` (a script that writes a receipt file): assert the
   game exits and the receipt appears.
6. **Current leg** — `latest_version == RBBLITZ_GAME_VERSION`: no START prompt on the screen.

The script drives the UI; per the owner's standing directive, ask before running it (or run it only
when the owner says "work autonomously").

---

## 13. Documentation owed when the work lands

| Document | Change |
| --- | --- |
| `docs/engine/messages.md` | The inbox study measured today: its Open list is what M2/M3 close. Extend it with the feed's seams, the draws and their evidence as they land. |
| `docs/engine/diagnostics.md` | `messages_api_url`, `messages_assets_dir`, `messages_updater`, the `messages:` log lines, the acceptance harness. |
| `docs/engine/toggles.md` | No new toggle (the feature is built-in); the *Show Messages* row already exists. Add one sentence to the row naming the feed. |
| `docs/reference/known-issues.md` | Whatever the spikes leave open (e.g. M3-C's text-only state, the elevated-install updater path). |
| `docs/README.md` + `docs/plans/todo/README.md` | The plan row (this file), and the engine/diagnostics rows if a new document appears. |
| `workers/messages/README.md` | The contract, the icon rules, `check.mjs`, local dev, the deploy command and the owner-approval rule. |
| `docs/guides/distributable.md` | `assets/messages/` becomes part of the payload (the audit admits it, `make_payload.ps1` copies it). |
| `installer/README.md` | Only if D9 changes the release model (publishing `update.toml` + the payload archive). |
| `docs/guides/build-and-run.md` | Only if the version read introduces a configure gate worth naming. |

Code comments cite this plan as `docs/plans/todo/2026-10-23-messages-and-update-feed-plan.md` §n and
must be re-pointed by `move_docs.py --map` when the plan moves to `in-progress/`.

---

## 14. Open questions for the owner

| Id | Question | Recommended |
| --- | --- | --- |
| Q1 | Does a release publish `update.toml` + a payload archive so the dormant updater can install? Or should `Update Game` open the release page instead? | Publish both; the flow the request describes needs them. |
| Q2 | Should the date be part of the row text (D10) or should the detail pane carry a `DATE` line above the body? | Row text; no scene edit. |
| Q3 | With no update, is removing the START prompt (and the press doing nothing) acceptable, or should START keep opening Help & Options invisibly? | Remove both; Help & Options stays on the main menu and the pause menu. |
| Q4 | Is a `wrangler` deploy of a public read-only worker acceptable, or should the document live in KV/R2 behind a token? | Static assets + Worker; no secrets. |
| Q5 | Should the feed be reachable when *Show Messages* is off? | Yes — the version check must run regardless; the tab stays hidden. |
| Q6 | Default icon when the field is missing or unresolvable: `icon_misc` (the neutral one), or should the row draw **no** icon? | `icon_misc`, pending the screen check. |
| Q7 | Does `assets/messages/` ship with the installer, or should players drop files there themselves? | Ship it (empty but present), so the folder is discoverable; the payload audit admits it. |

---

## 15. Reproducing this session's measurements

```powershell
# 1. The retail inbox record and scene, straight out of the main ark (gitignored output)
python -c "import sys; sys.path.insert(0,'scripts'); import hmx_ark; h=hmx_ark.load_header('game/gen/main_xbox.hdr'); parts=h.ark_parts('game/gen/main_xbox.hdr'); f=open(parts[0],'rb'); e=[x for x in h.files if x.path=='ui/inbox/gen/inbox.dtb'][0]; i,off=h.select_ark(e.offset); f.seek(off); open('out/_swap/inbox_retail.dtb','wb').write(f.read(e.size))"
python out\_swap\dtb_tree.py out\_swap\inbox_retail.dtb > out\_swap\inbox_retail.tree.txt

# 2. The scene's object names and embedded textures
python scripts\hmx_milo.py list    out\_swap\inbox_retail.milo_xbox
python scripts\hmx_milo.py records out\_swap\inbox_retail.milo_xbox

# 3. Names, addresses and vtables in the decrypted image
python -c "data=open('out/_swap/blitz_image.bin','rb').read(); import re; [print(hex(0x82000000+m.start()), m.group().decode('latin1')) for m in re.finditer(rb'[ -~]{4,120}', data) if b'nbox' in m.group()]"
python out\_swap\rtti_vtables.py InboxProvider 24
python out\_swap\find_str_refs.py 0x82042F04 0x82042EC8 0x82042EE0 0x82042EF0

# 4. The row icon set: the scenes' assets, the inline bitmaps and the converted PNGs
python scripts\hmx_milo.py list out\_swap\list_inbox.milo_xbox
python scripts\ark_inline_icons.py --hdr game/gen/main_xbox.hdr --out out/_swap/messages-icons
Get-ChildItem out\_swap\messages-icons\*.png_xbox |
    ForEach-Object { python scripts\hmx_tex.py export $_.FullName --out $_.FullName.Replace('.png_xbox', '.png') }
```

The tree dump in step 1 is the source of §2.1's statements; §2.2's addresses come from step 3 and the
generated sources (`rb_blitz_recomp.17.cpp` for `build_list`, `…37.cpp` for the `InboxMessage`
constructor, `…46.cpp` for the provider's message handler). Step 4 is §2.3's icon set; the extraction
is proven exact because `icon_more_stars`'s inline bytes equal the shipped
`ui/recommendations/img/gen/icon_more_stars_keep.png_xbox` byte for byte.
