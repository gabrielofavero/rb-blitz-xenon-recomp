# The Messages screen (retail inbox)

How the title's MESSAGES tab is built and filled, what was measured about it on 2026-10-23, and what
is still unknown. This is the study the messages feed work builds on; the plan is
[`../plans/todo/2026-10-23-messages-and-update-feed-plan.md`](../plans/todo/2026-10-23-messages-and-update-feed-plan.md).

Evidence is regenerated on demand under the gitignored `out/_swap/` (§7).

## 1. The screen and its record

The screen is `inbox_screen`, declared in `ui/inbox/gen/inbox.dtb` (10,339 bytes in the retail
`game/gen/main_xbox.hdr`). `[measured 2026-10-23, `out/_swap/inbox_retail.tree.txt`]`

```
{new UIScreen inbox_screen
  {panels background_panel shell_music_panel menu_cache_panel inbox_panel error_popup_panel}
  {header_index 3}                ; the library header ring: Your Songs → RhythmVerse → Messages → Recommended
  {back_screen splash_screen}
  ...}
```

`inbox_panel` is `InboxPanel` over `inbox.milo`:

- `{file inbox.milo}`, `{focus songs.lst}`, `{read_message_delay 1000}`;
- `enter` → `{this get_inbox_provider}` → `build_list`;
  `{songs.lst set_provider {this get_inbox_provider}}`;
  `{this get_inbox_provider} update_subject_message subject.lbl message.lbl {songs.lst selected_pos}`;
  `update_inline_help`; loading/scroller visibility;
  `update_participating_user_presence CONTEXT_PRESENCE_RP_EXPLORING_MENU`;
- `on_messages_updated` → the same `build_list` + `update_subject_message` + `{songs.lst refresh}`;
- `SCROLL_MSG` → `start_msg_timer`, `update_subject_message`, `update_inline_help` for the newly
  selected row.

So the list is a retail `UIList` (`songs.lst`) over a native provider, and the right-hand pane is two
labels (`subject.lbl`, `message.lbl`) filled by that provider from the selected row.

## 2. The provider and its message data

| Fact | Value |
| --- | --- |
| `InboxPanel` RTTI / primary vtable | `0x82808d38` → `0x8203ce4c` |
| `InboxPanel` method names | `0x8203d148`-`0x8203d308`: `cancel_challenge_complete`, `on_messages_updated`, `show_gamercard`, `get_challenge_id`, `is_active_challenge`, `can_cancel_challenge`, `cancel_selected_challenge`, `start_msg_timer`, `get_inbox_provider`, `is_playable`, `get_msg_song_metadata`, `get_msg_song_offer_id`, `is_rbw_upsell`, `is_buy_full_game`, `is_song_bonus`, `is_challenge`, `is_store_offer` |
| `InboxProvider` RTTI / vtables | `0x82809240` → primary `0x82042a4c`; secondary (UIListProvider) `0x820429ec` |
| `InboxProvider` DTA names | `build_list` `0x82042f04`, `get_header_token` `0x82042ef0`, `get_msg_type` `0x82042ee0`, `update_subject_message` `0x82042ec8` |
| Provider message handler | `sub_822D6598` (`generated/default/rb_blitz_recomp.46.cpp`): interns a name, then dispatches per branch |
| `build_list` | `sub_822D5928` (`rb_blitz_recomp.17.cpp`) |
| `InboxMessage` RTTI / constructor | `0x828347ac` / `sub_82747578` (`rb_blitz_recomp.37.cpp`) |
| `InboxMessageMgr` RTTI | `0x8283478c` |
| `GetInboxJob` RTTI | `0x8280a490`; the job's endpoint string is `remoteconfig/getinboxmessages/` at `0x82059f34`, in a `const char*` table based at `0x82059fac` |

`build_list`'s body, read from the generated code `[static]`:

1. Walks the provider's vector at `this+52…this+56`, calls `sub_824AF4E0` on each element, and clears
   it with `sub_823A0158`. The vector therefore holds **pointers to 16-byte heap entries**, and the
   clear loop frees those entries (not the messages).
2. If `sub_821CDCF0(*(void**)0x82805C80)` answers true, pushes **one** entry for the **trial-upsell**
   state, built from `upsell_title_inbox` / `upsell_text_inbox` (type `10`, `true`, `-1`, `-1`).
3. Otherwise reads the `InboxMessageMgr` at `provider+48`: `sub_822B8BD8(mgr)` = count,
   `sub_82746950(mgr, index)` = message pointer, and pushes one entry per message:
   `{+0 u32 symbol, +4 InboxMessage*, +8 message->+60, +12 u8 message->+138}`.
4. **If the count is 0**, builds the default message from `default_inbox_subject` /
   `default_inbox_message` (type `1`, `false`, `-1`, `-1`), appends it to the manager
   (`sub_82746DA0(mgr, message, 1)`) and pushes `{symbol, message, 1, 0}`. This is the screenshot's
   "No messages" state, and the only measured recipe for a plain text message.

The `+0` symbol is an interned symbol read from the pointer at `0x8280A594` (`build_list`,
`lwz r4,-23148(r27)` with `r27` = `0x82810000`), the same in all three branches.

`InboxMessage`'s constructor `sub_82747578(this, String r4, int r5, String r6, bool r7, int r8, int r9)`
stores `r4` at `+44` (subject `String`), `r5` at `+60`, `r8`/`r9` at `+52`/`+56`, `r6` at `+64`
(body `String`), `r7` at `+138`; it initialises four empty vectors at `+72…+100`, `+112` = 0 and two
6-byte buffers at `+139`/`+145`. The trial-upsell entry passes type `10`, `true`, `-1`, `-1`; the
empty-state entry passes `1`, `false`, `-1`, `-1`.

## 3. The empty state

`No messages` / `Sorry, you don't have any messages at this time.` are **locale entries**, not scene
labels `[measured 2026-10-23, `out/_swap/locale_eng.txt`]`:

```
[symbol] default_inbox_subject   [string] No messages
[symbol] default_inbox_message   [string] Sorry, you don't have any messages at this time.
```

The provider appends that message to the manager when the manager is empty, so the empty state is a
real `InboxMessage` (type `1`, "not challenge") rather than a label. That is the state in the owner's
screenshot, and it is what a boot with no feed must keep showing.

## 4. The bottom bar

The bar is the scene's `InlineHelp.ihp` fed by the panel's `update_inline_help`, which **clears** the
tokens for `kAction_Confirm`, `kAction_Cancel`, `kAction_Start`, `kAction_Option` and
`kAction_ShellOption`, then sets `kAction_Cancel → inline_help_back` and
`kAction_Start → inline_help_options`. `inline_help_options` is `HELP & OPTIONS` in the English
locale. `[measured 2026-10-23, `out/_swap/inbox_retail.tree.txt`]`

The press itself is a case in `inbox_panel`'s `BUTTON_DOWN_MSG`:

```
{kAction_Start
  {options_screen set back_screen inbox_screen}
  {ui goto_screen options_screen}}
```

Both the label and the case live in the same record the settings payload already rewrites for the
library header (`src/settings/menu_payload.cpp`), so a different prompt for this screen is a record
edit, not a new panel.

## 5. The row icon

The message pane itself has no picture slot (`inbox.milo_xbox`'s objects are the pane: textures
`blank.tex`, `frame.tex`, `neon.tex`; materials `frame.mat`, `message_pane.mat`, `nav_pane.mat`,
`neon.mat`, `rail.mat`, `scroller.mat`, `subject_pane.mat`; groups `message.grp`, `message_gfx.grp`,
`Challenge.grp`, `Waiting.grp`; labels `subject.lbl`, `message.lbl`, `challenge_txt.lbl`,
`waiting_txt.lbl`; meshes `message_pane.mesh`, `subject_pane.mesh`). `[measured 2026-10-23,
`out/_swap/inbox_retail.milo_xbox`]`

**The row icon does exist, and it is a named set.** `ui/resource/lists/gen/list_inbox.milo_xbox`
(441,418 B) is a `UIListDir` (`default`) with a `UIListMesh` named `icon` (`icon.mesh`),
`subject_line.lbl`, `time.lbl`, the `slot.mesh`/`slot.mat` highlight chrome, twelve `icon_*.mat`
materials and thirteen `icon_*.tex` textures. `ui/recommendations/gen/rec_icon.milo_xbox` (420,996 B)
is the icon proxy: `rec_icon.mat` + `rec_icon.mesh` + an `icon_control` flow over the same textures,
which is where the bitmaps live. `[measured 2026-10-23]`

### 5.1 The twelve bitmaps and the one declared without one

Each icon is one chunk of `rec_icon.milo_xbox`: a header, the source path, then a **512×512 DXT1**
mip chain (mips 512..16 = 174,720 bytes; header `bpp = 8`, `mips−1 = 5`, width/height/pitch
512/512/256, big-endian). `scripts/assets/ark_inline_icons.py` dumps them; `scripts/assets/hmx_tex.py export`
writes the PNGs to `out/_swap/messages-icons/`. The extraction is **proven exact**: the inline bytes
of `icon_more_stars` equal the shipped `ui/recommendations/img/gen/icon_more_stars_keep.png_xbox`
byte for byte (174,752 B including the 32-byte envelope). `[measured 2026-10-23]`

| Tex name (feed id) | Source in the scene | Row material | What it draws | Converted PNG |
| --- | --- | --- | --- | --- |
| `icon_friend_popular` | `img/icon_friend_popular.png` | `icon_friend_popular.mat` | a star holding a person silhouette, sweeping lines | `out/_swap/messages-icons/icon_friend_popular.png` |
| `icon_friends_play` | `img/icon_friends_play.png` | `icon_friends_play.mat` | two people side by side | `out/_swap/messages-icons/icon_friends_play.png` |
| `icon_misc` | `img/icon_mix.png` | `icon_misc.mat` | a radio tower with radiating lines | `out/_swap/messages-icons/icon_mix.png` |
| `icon_more_stars` | `img/icon_more_stars.png` | `icon_more_stars.mat` | stars in ovals of growing size | `out/_swap/messages-icons/icon_more_stars.png` |
| `icon_new_release` | `img/icon_new_release.png` | `icon_new_release.mat` | a vinyl record half out of its sleeve | `out/_swap/messages-icons/icon_new_release.png` |
| `icon_revisit_song` | `img/icon_revisit_song.png` | `icon_revisit_song.mat` | two loop arrows around a hatched diamond | `out/_swap/messages-icons/icon_revisit_song.png` |
| `icon_store_plays` | `img/icon_store_plays.png` | `icon_store_plays.mat` | a tilted music staff with notes | `out/_swap/messages-icons/icon_store_plays.png` |
| `icon_tournament` | `img/icon_tournament.png` | `icon_tournament.mat` | a circle with two people, sunburst rays | `out/_swap/messages-icons/icon_tournament.png` |
| `icons_decrease_rank` | `img/icons_decrease_rank.png` | `icon_decrease_rank.mat` | two arrows pointing down-left | `out/_swap/messages-icons/icons_decrease_rank.png` |
| `icons_increase_rank` | `img/icons_increase_rank.png` | `icon_increase_rank.mat` | two arrows pointing up-right | `out/_swap/messages-icons/icons_increase_rank.png` |
| `icons_store_rec` | `img/icons_store_rec.png` | `icon_store_rec.mat` | a globe with networked avatars radiating upward | `out/_swap/messages-icons/icons_store_rec.png` |
| `icons_unplayed_songs` | `img/icons_unplayed_songs.png` | *(none in `list_inbox`)* | a crate with a lattice front | `out/_swap/messages-icons/icons_unplayed_songs.png` |
| `icon_challenge` | `img/icon_challenge.png` | `icon_challenge.mat` | — | **no bitmap in the main ark** (declared by both scenes; its source is unproven) |

All twelve converted images on one sheet: `out/_swap/messages-icons/contact-sheet.png`. The icons are
black-and-white **masks**; the list tints them. For context, the row's non-icon textures are
`slot.png`/`slot_bar.png` (512×128, embedded in `list_inbox`) and `frame.tex`/`neon.tex`
(1024×1024, embedded in `inbox.milo`) — the row chrome and the pane's frame/glow.

### 5.2 How the row picks an icon — unproven

`build_list` puts the same symbol in `+0` of every 16-byte entry (interned from `0x8280A594`), so the
selection is not that field. It is either the message type (`+60`, the value the `is_*` getters read)
or something inside `UIListMesh` (its `default_mat`). No boot has shown two different icons on two
inbox rows yet.

### 5.3 A custom icon would still need a texture seam

There is no authoring path for scene objects (`tools/milotex` only replaces an embedded texture's
DDS). Loading a new 512×512 texture at runtime and binding it to the row's icon material is
unproven; the candidates are `Tex`'s DTA `set_file` `0x82020908` / `set_bitmap` `0x82020928` with
`RndMat`'s `diffuse_tex` (`0x820232b0`/`0x820232c8`, which the store's `LinkAlbumArt` already sets
from DTA), and the album-art netcache seam (`sub_82469C28` → completion `sub_82445CD8` into
`client+100`'s `RndTex`).

## 6. What the retail inbox speaks (strings only)

The image contains the retail web-service vocabulary around the inbox endpoint `[static, string scan
2026-10-23]`:

- endpoint table at `0x82059fac`: `remoteconfig/getinboxmessages/`, `remoteconfig/getrecommendations/`,
  `remoteconfig/getconfig/`, `remoteconfig/linkemail/`, `remoteconfig/linkfacebook/`,
  `remoteconfig/unlinkfacebook/`, `auth/getwebsitepasscode/`, `economy/purchasepowerups/`,
  `challenges/challenge/`, `datapoint/record/purchase/`;
- field names near `0x8205a1cc`: `message`, `subject`, `inbox_type`, `payload`, `challenge_type`,
  `(unknown)`, `messages`, plus `recommendations`, `server_dta`, `friend_guids`.

So retail already parses a JSON inbox (`messages[]` with `subject`/`message`/`inbox_type`/`payload`).
Whether that parse is reachable offline is **unproven**: the job class `GetInboxJob` sits on the same
netcache/web-service stack the store measured as never issuing I/O on an offline boot
([`rhythmverse-store-plan`](../plans/finished/2026-10-13-rhythmverse-store-plan.md) §5/P2).

## 7. Reproducing the measurements

```powershell
# the record (gitignored output)
python -c "import sys; sys.path.insert(0,'scripts'); import hmx_ark; h=hmx_ark.load_header('game/gen/main_xbox.hdr'); parts=h.ark_parts('game/gen/main_xbox.hdr'); f=open(parts[0],'rb'); e=[x for x in h.files if x.path=='ui/inbox/gen/inbox.dtb'][0]; i,off=h.select_ark(e.offset); f.seek(off); open('out/_swap/inbox_retail.dtb','wb').write(f.read(e.size))"
python out\_swap\dtb_tree.py out\_swap\inbox_retail.dtb > out\_swap\inbox_retail.tree.txt

# the scene
python -c "import sys; sys.path.insert(0,'scripts'); import hmx_ark; h=hmx_ark.load_header('game/gen/main_xbox.hdr'); parts=h.ark_parts('game/gen/main_xbox.hdr'); f=open(parts[0],'rb'); e=[x for x in h.files if x.path=='ui/inbox/gen/inbox.milo_xbox'][0]; i,off=h.select_ark(e.offset); f.seek(off); open('out/_swap/inbox_retail.milo_xbox','wb').write(f.read(e.size))"
python scripts\assets\hmx_milo.py list out\_swap\inbox_retail.milo_xbox
python scripts\assets\hmx_milo.py records out\_swap\inbox_retail.milo_xbox

# the row icon set: the list template, then the inline bitmaps and their PNGs
python -c "import sys; sys.path.insert(0,'scripts'); import hmx_ark; h=hmx_ark.load_header('game/gen/main_xbox.hdr'); parts=h.ark_parts('game/gen/main_xbox.hdr'); f=open(parts[0],'rb'); e=[x for x in h.files if x.path=='ui/resource/lists/gen/list_inbox.milo_xbox'][0]; i,off=h.select_ark(e.offset); f.seek(off); open('out/_swap/list_inbox.milo_xbox','wb').write(f.read(e.size))"
python scripts\assets\hmx_milo.py list out\_swap\list_inbox.milo_xbox
python scripts\assets\ark_inline_icons.py --hdr game/gen/main_xbox.hdr --out out/_swap/messages-icons
Get-ChildItem out\_swap\messages-icons\*.png_xbox |
    ForEach-Object { python scripts\assets\hmx_tex.py export $_.FullName --out $_.FullName.Replace('.png_xbox', '.png') }

# proof: icon_more_stars' inline bytes == the shipped file's bytes
python -c "import sys; sys.path.insert(0,'scripts'); import hmx_ark; h=hmx_ark.load_header('game/gen/main_xbox.hdr'); parts=h.ark_parts('game/gen/main_xbox.hdr'); f=open(parts[0],'rb'); e=[x for x in h.files if x.path=='ui/recommendations/img/gen/icon_more_stars_keep.png_xbox'][0]; i,off=h.select_ark(e.offset); f.seek(off); s=f.read(e.size); i2=open('out/_swap/messages-icons/icon_more_stars.png_xbox','rb').read(); print('identical:', s==i2)"

# names, addresses, vtables
python out\_swap\rtti_vtables.py InboxProvider 24
python out\_swap\find_str_refs.py 0x82042F04 0x82042EC8 0x82042EE0 0x82042EF0
```

## 8. Open

- **How the row picks an icon** is unproven (§5.2). The type (`+60`) or `UIListMesh`'s own state is
  the likely driver; no boot has shown two different icons yet.
- **`icon_challenge`'s bitmap** is not in the main ark, although both scenes declare the texture and
  the material. Where the challenge icon comes from is unknown.
- **No custom texture has ever been loaded into the inbox:** the `Tex::set_file` route and the
  netcache client route are both candidates (§5.3), neither measured.
- **The real inbox types** (the JSON's `inbox_type`) are unmapped. Only two values are measured:
  `1` = a plain text message (the empty state) and `10` = the trial upsell. The getters
  `is_challenge`, `is_store_offer`, `is_song_bonus`, `is_rbw_upsell`, `is_buy_full_game` read fields
  we have not walked.
- **Whether the label setters localize raw text**: the default message draws locale tokens; offer
  titles draw raw strings through a different provider. Unproven for this provider.
- **`sub_821CDCF0`'s meaning** (the gate between the trial-upsell entry and the manager list) is
  unproven; the branch shape is measured, the predicate is not.
- **The `+0` symbol** of a 16-byte entry is interned from the pointer at `0x8280A594` (`build_list`,
  `lwz r4,-23148(r27)` with `r27` = `0x82810000`); what that symbol names is unproven.
- **`build_list` mutates the manager when it is empty**: the default message is appended with
  `sub_82746DA0(mgr, message, 1)`. Anything that injects messages must expect that object to already
  be there.
- **No image surface** in the message pane (§5): no picture plane, texture slot or image label
  exists in `inbox.milo`, so only the row icon is a candidate for per-message art.
- **The retail JSON parse** (§6) has never been fed offline; the request plumbing (base URL, auth,
  response object) is unmapped.

## 9. Implementation

The messages feed (worker, boot fetch, cache, list injection, image, update prompt) is planned in
[`../plans/todo/2026-10-23-messages-and-update-feed-plan.md`](../plans/todo/2026-10-23-messages-and-update-feed-plan.md).
Nothing in this document depends on that plan being executed.
