# Music library difficulty-sort crash

The owner's 2026-10-24 reproduction is Artist → Song Name → Difficulty, with
Select crashing on the second press before the Difficulty view draws.
`rb_blitz_322.log` records an invalid guest read at `0x72616E6F`.
The diagnostic build's `rb_blitz_323.log` confirms `sort_diff` finishes its native
sort before the same fault occurs while constructing the group headings.

The recorded PC and stack resolve to `DataArray::FindArray` (`0x823FAE00`),
called through `0x821C9550` from the difficulty-heading lookup `0x821D0F30`.
The original rank calculation (`0x821D16A8`) returns 0 for a band rank below
the first threshold (1), including missing/zero ranks. The heading lookup uses
that value directly as an index into `song_groupings` → `rank`.
Slot 0 is the symbol `rank`; the seven tier arrays occupy slots 1 through 7.
Interpreting the symbol text as a DataArray leads to reading the four bytes
`rank` as a pointer, then dereferencing it plus four: `0x72616E6B + 4`.

`src/hooks/song_sort.cpp` overrides only `0x821D0F30`: a nonpositive heading
index uses slot 1, the lowest difficulty heading. Every normal tier still uses
the original lookup. The rank calculation, numerical sorting, metadata and
installed game data are unchanged. This fix is unconditional for both retail
and Ultimate content.

`tests/song_sort_tests.cpp` executes the actual generated retail rank and
heading routines with an isolated synthetic table and the production hook.
`song_sort_guest_fixture.py` extracts the routines from codegen output so the
test does not maintain a second translation. The fixture supplies config lookup
and direct evaluation of its constant DataNodes. It neither launches nor
navigates the game.

The 101 checks reproduce the exact original invalid read with Windows SEH,
verify missing/zero/fractional ranks below 1, exercise both sides of all seven
band thresholds, compare valid headings against the original lookup, and check
the output node type and restored stack pointer. Run after building:

```powershell
ctest --test-dir out/build/win-amd64-release -R '^song_sort$' --output-on-failure
```

The owner's in-game confirmation of the corrected Difficulty view is pending.
The temporary vectored fault logger used for `rb_blitz_323.log` was removed;
it also observed ordinary faults that the runtime subsequently handled.
