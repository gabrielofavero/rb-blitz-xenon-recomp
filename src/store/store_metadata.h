// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The store's data model and record writer (RhythmVerse store plan §5/P2-P3).
//
// Measured 2026-10-20 in boot log `rb_blitz_055.log` (17:10:11-17:11:04; the store entry was
// driven by the P4 search diagnostic's `enter` statements, but the request itself is the retail
// `request_index_if_needed` path):
//
//   * the panel's first request is `dlc_store/<region>/dlc_top_<platform>_<locale>.dta`, with the
//     substitutions `europe`, `xbox`, `eng` on this build;
//   * the builder that produces it is `sub_8274FB60`, and the `request_index_if_needed` handler
//     (`sub_82753010`) reaches it once the state word at `0x82853D4C` is 2 - the first P3 attempt
//     forced `0x828A3D4C`, an address that is not this word (`lis -32123` is 0x8285, not 0x828A).
//
// The front-menu record's shape is read out of the handler that consumes it
// (`sub_8275D4E0` + `sub_8275CBD8`): the record is a DTA array whose `submenus` value is an array
// of provider paths, and each entry is compared against `/<locale>/<name>/` (the format
// `sub_8275CBD8` builds with `sprintf("/%s/%s/", locale, name)`). The menu label comes from the
// provider's own `get_string`, which the DTA turns into the `menu_<name>` source id - so the
// entry's *path* is what the panel fetches next, through the same `/base + provider` builder:
// `dlc_store/<region>/<locale>/<name>/`.
//
// The menu record is consumed by `sub_8274F610`, which reads `offers`, `offers_populated`,
// `sorted`, `next_chunk`, `previous_chunk` and `index_info` from the fetched array; each offer is
// a DTA map whose keys are the ones the decoded scripts read (`name`, `artist`, `album`, `genre`,
// `year_released`, `preview_art`, `rank <part>` - plan §2.4).
//
// What is **not** proven: that the native parser accepts these exact key spellings. The fetch
// now happens and the requested name is known, but the local-server delivery of a record has not
// been observed (`ui/store/remote_metadata/**` opens are not in the boot log yet), so the writer
// stays behind this one module and its tests pin the measured names, not the retail parser.
//
// SDK-free on purpose: pure bytes plus string templates, so tests/store_metadata_tests.cpp
// covers it without a boot.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "store/rhythmverse_client.h"

namespace rb_blitz::store {

// The substitutions measured in the first request. Kept as constants so a different region or
// language build changes one place; the values are evidence, not a guess.
inline constexpr char kStoreRegion[] = "europe";
inline constexpr char kStorePlatform[] = "xbox";
inline constexpr char kStoreLocale[] = "eng";

// One row of the store's front menu, in the order the player sees it.
struct FrontMenuEntry {
  // The menu's provider name; the panel's path is `/<locale>/<name>/` and its source id is
  // `menu_<name>`.
  std::string id;
  // The token or literal the row draws. The retail provider reads it from the locale record
  // (`menu_<id>`); the writer carries it so a served front menu names its own rows.
  std::string label;
};

// The front menu the owner asked for (§1): Recent and Search, nothing else. Retail's categories
// are intentionally not reproduced.
struct FrontMenu {
  std::string region = kStoreRegion;
  std::string platform = kStorePlatform;
  std::string locale = kStoreLocale;
  std::vector<FrontMenuEntry> entries;
};

// `dlc_store/<region>/dlc_top_<platform>_<locale>.dta` - the panel's index request.
std::string FrontMenuRecordPath(std::string_view region = kStoreRegion,
                                std::string_view platform = kStorePlatform,
                                std::string_view locale = kStoreLocale);

// The entry the panel's front menu carries and compares: `/<locale>/<name>/`.
std::string MenuProviderPath(std::string_view locale, std::string_view name);

// The request the panel builds from the base and that provider:
// `dlc_store/<region>` + `/<locale>/<name>/` = `dlc_store/<region>/<locale>/<name>/`.
std::string MenuRecordPath(std::string_view region, std::string_view locale, std::string_view name);

// `dlc_store/<platform>/<locale>/offers/<offer_id>/` - the offer's asset directory.
std::string OfferDirectory(std::string_view platform, std::string_view locale,
                           std::string_view offer_id);

// `dlc_store/<platform>/<locale>/related/<offer_id>.dta` - related offers.
std::string RelatedOfferPath(std::string_view platform, std::string_view locale,
                             std::string_view offer_id);

// The panel's source id for a menu: `menu_<entry id>`.
std::string MenuSourceId(std::string_view entry_id);

// The owner's two menus, in order.
FrontMenu RhythmVerseFrontMenu();

// The front-menu writer. The record is the shape `sub_8275D4E0` reads:
//
//   (submenus ("/<locale>/<id>/" ...))
//
// Returns an empty vector and fills `why` when the menu has no entries or an entry has an empty
// id or label - a record with a nameless row would make the panel fetch `/<locale>//`, which is a
// worse failure than not shipping one.
std::vector<uint8_t> BuildFrontMenuRecord(const FrontMenu& menu, std::string* why);

// One song offer, with the fields the decoded scripts read (plan §2.4). Optional fields are left
// out of the record when empty, which is how the retail DTA hides the preview UI
// (`!{offer has_data preview_audio}`) and why no preview path is written.
struct Offer {
  std::string name;
  std::string artist;
  std::string album;
  std::string genre;
  // 0 means "unknown" and is omitted: a zero year is not a release date.
  int year_released = 0;
  // A path the art loader can fetch (`store_panel load_art <path> this`); empty hides the art.
  // The panel composes `request prefix + this value`, so the value is `offers/<key>/cover_art.png_xbox`
  // (src/store/album_art.h owns the spelling) and the bytes are answered by the serving policy.
  std::string preview_art;
  // The catalogue's own `album_art` field (plan §1.3), as the API returned it. **Never written to a
  // record**: it is the host's half of the pair whose guest half is `preview_art`, consumed by the
  // art worker (src/store/album_art_service.h) to fetch and encode the texture.
  std::string art_url;
  // The preview audio file name (`store_preview_mgr`). RhythmVerse supplies none (§3.3), but the
  // key must still exist: `store_info_panel.refresh_buttons` reads `get_data (preview_audio)`
  // **unconditionally** for a song, and a missing key makes the offer walk dereference null
  // (measured 2026-10-21, the A press on a served offer). Retail's own offers always carry it.
  std::string preview_audio;
  // The per-part rank the catalogue publishes (an online rank, not a tier). Written as the
  // retail-shaped `(rank ((guitar N) ...))` map, which is the schema the shipped records carry and
  // what the browser's `rank band` grouping reads. 0 means "unknown" and is written as 0, not
  // omitted: a missing part hands the retail walk a null.
  int rank_guitar = 0;
  int rank_drum = 0;
  int rank_bass = 0;
  int rank_vocals = 0;
  int rank_keys = 0;
  // The catalogue's own difficulty **tier** per part (`diff_*` in the API, 0..6). This is what the
  // store's difficulty meter draws, as `(diff_<part> N)` keys: the panel's
  // `{this set_inst_difficulty <id> <tier> TRUE}` takes a tier, and measured 2026-10-21 the asset it
  // drives has exactly five pips plus a devil icon (`ui/difficulty_meter/gen/diff_meter_all_inst.milo`:
  // `diff_1.mesh` ... `diff_5.mesh`, `icon_devil.tex`) - the 0..6 scale the `diff_*` fields use,
  // where 0 means the song has no chart for that part and the row reads `NO PART` (`no_part` in the
  // locale). Retail's own path converts an *online rank* to a tier with `song_mgr rank_tier`, which
  // needs thresholds this build does not have; the catalogue already publishes the tier, so the
  // payload hands that through instead (store_payload.h's `UnrollOfferDifficulty`).
  int diff_guitar = 0;
  int diff_drum = 0;
  int diff_bass = 0;
  int diff_vocals = 0;
  int diff_keys = 0;
  // True when the song is already in the loaded DLC library (plan §3.2). It is written as the
  // offer's `is_purchased` key, which is what the retail scripts read as the Downloaded state:
  // `refresh_buttons` inserts `store_download_again` for a purchased offer and nothing but Exit
  // otherwise, and `purchase_allowed` (and with it the price chrome) needs `is_available`, which
  // a free custom never sets. A receipt never sets this flag - only the loaded library does.
  bool downloaded = false;
};

// The menu record shape `sub_8274F610` reads:
//
//   (offers_populated TRUE)
//   (sorted TRUE)
//   (offers ((name "...")(artist "...") ...) ...)
//
// An empty `offers` array is a valid page (P3's exit requires empty/filtered-only pages to work);
// a nameless offer is refused. `next_chunk`/`previous_chunk`/`index_info` are deliberately absent:
// they belong to paginated retail menus, and this writer serves one complete page.
std::vector<uint8_t> BuildMenuRecord(const std::vector<Offer>& offers, std::string* why);

// One catalogue record as the store's record model (P3's field mapping, plan §5/P3).
//
// The mapping is the plan's first cut, now anchored to the retail script that reads it:
// `store_details_panel refresh` asks the offer for `genre`, `year_released`, `preview_art` and - for
// a song - one difficulty value per part. That block is the one part of the retail script this
// build does **not** run as written: it reads `{offer get_data (rank <part>)}` (a two-component
// path the offer walk cannot resolve, store_payload.h's `UnrollOfferDifficulty`) and converts the
// rank to a tier with `song_mgr rank_tier`. The payload unrolls it onto the record's own `diff_<part>`
// keys, which carry the tier the catalogue publishes - the same 0..6 scale the meter asset draws
// (`Offer::diff_guitar`). `rank_*` is still written: it is the retail schema, and the browser's
// `rank band` grouping reads the `(rank (...))` map.
//
// `preview_art` is the served texture's path when the API names an album-art cover and the art
// worker can encode it (`AlbumArtEnabled`), empty otherwise - an absent key is what makes the retail
// panel draw its own placeholder art instead of asking for a file nobody serves. `art_url` carries
// the catalogue field to that worker and is never written to a record.
Offer OfferFromSong(const SongFile& song);

// The same two shapes in **text**, which is what the netcache serves. Measured 2026-10-20 in the
// decrypted image: the loaded-file insert (`sub_824685E0`) classifies a request path by its
// extension (`strncmp(extension, "dta", 3)`), and `.dta` is the text parser's type - so a served
// record is DTA *text*, not a compiled `.dtb` stream. The compiled writers above stay for records
// that go into an ark (the payload).
//
// Values are written in double quotes with `\` and `"` backslash-escaped. `BuildFrontMenuText`
// refuses an empty menu or a nameless entry (the panel would draw nothing and never populate);
// `BuildMenuText` accepts an empty offer list, which is a valid, empty page.
std::string BuildFrontMenuText(const FrontMenu& menu, std::string* why);
std::string BuildMenuText(const std::vector<Offer>& offers, std::string* why);

// The `version` value the front-menu writer emits. P3 bumps it if the schema changes once the
// parser has been observed; it exists so a served record can be told apart from an older one in a
// log.
inline constexpr uint32_t kFrontMenuVersion = 2;

// The node kind of the offer's `type` key:
//
//   * as a **symbol** (`(type song)`) the retail provider answers the row label - measured 2026-10-21,
//     `StoreOfferProvider::Text` (`sub_8275A230`) reads every one of our titles through
//     `get_data("name")` (`store-probe: offer name exit '<title>'`), because it compares
//     `get_data("type")` against the interned symbol `song` (`sub_822D1CE0`);
//   * as a **string** (the shipped `dummy_upsell_offer`'s spelling) the provider falls through to
//     its empty answer and every row draws blank - the pre-P3 behaviour.
//
// The symbol form is the shipped default. It used to fault while the list drew, and that null was
// the details panel's `(rank <part>)` read - not the label - which the payload now unrolls onto the
// record's `rank_<part>` keys (store_payload.h). `store_offer_type_symbol` (src/store/store_fetch.cpp)
// can still select the string form for a boot.
void SetOfferTypeSymbol(bool symbol);

}  // namespace rb_blitz::store
