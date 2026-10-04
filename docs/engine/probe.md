# The probe — what the guest is doing, at runtime

The instrument behind the flow maps in this directory and behind
[plans/customization-plan.md](../plans/customization-plan.md) P2. It answers one question — *in what
order did the guest reach the points I care about, with what arguments* — without a debugger, without
an instruction trace, and without reading guest memory.

Code: [src/diag/probe.h](../../src/diag/probe.h) (interface), [probe.cpp](../../src/diag/probe.cpp)
(cvars, the ring, recording), [probe_overlay.cpp](../../src/diag/probe_overlay.cpp) (the panel),
[probe_trace.h](../../src/diag/probe_trace.h) (the ring itself, SDK-free and unit-tested by
`tests/probe_trace_tests.cpp`), [probe_points.cpp](../../src/diag/probe_points.cpp) (the points that
exist today).

## The three things it is

1. **A ring of events.** `probe_trace_capacity` events (256 by default) in a bounded, thread-safe
   buffer. The oldest are overwritten and counted as dropped, so the buffer always holds the *newest*
   window — which is what a screen transition needs.
2. **A point.** One line inside a hook:

   ```cpp
   REX_HOOK_RAW(VorbisReader_CheckHmxHeader) {
     RBBLITZ_PROBE(0x82768C88, "mogg.header");
     __imp__VorbisReader_CheckHmxHeader(ctx, base);
   }
   ```

   `RBBLITZ_PROBE(address, tag)` records the address, a short tag, the calling thread, the guest
   return address (`lr`, i.e. who called it) and `r3..r10`. `RBBLITZ_PROBE_REGISTERS(tag)` does the
   same for a point with no address of its own, and `RBBLITZ_PROBE_MARK(tag)` is a breadcrumb from
   host code (a boot milestone) that lands in the same ordered trace.
3. **A view.** An ImGui overlay (below), and a trace file a script can read.

What it is not: it is not a profiler, it does not see a call that has no point on it (a hook is a
compile-time override of one function, so the trace is exactly the points you placed), and it does
not touch guest memory.

## Using it

| cvar | Default | What it does |
| --- | --- | --- |
| `probe_trace` | `false` | Records points into the ring. Off, a point costs a predictable-not-taken branch. |
| `probe_trace_capacity` | `256` | Ring size (min 16, max 1048576). Raise it to hold a whole transition; watch `dropped` in the file header or the panel. |
| `probe_trace_path` | `""` | When set, the newest events are rewritten to this file about twice a second, oldest first. This is the scriptable output. |
| `probe_overlay` | `false` | Draws the panel. **Live**: it is re-read every frame, so it can be switched on mid-run with no restart. |

All four go in the same `<exe name>.toml` as the other settings (see
[build-and-run.md](../build-and-run.md)), or on the command line, which is what a script uses:

```powershell
.\rb_blitz.exe --game_data_root=..\..\..\game --fullscreen=0 `
  --probe_trace=1 --probe_trace_capacity=8192 --probe_trace_path=probe-trace.log
```

The trace file is rewritten wholesale, so **the header line is the state of the run**, not of the
file: `# probe trace: 1810 event(s) held, 0 dropped, 1810 recorded since the last clear` says the ring
never wrapped. A non-zero `dropped` means the window you are reading is only the tail — raise the
capacity, or narrow the trace.

Reading one line:

```text
#000002 +    0.000s tF8000028 0x823DE0C0 entry "mogg.hv_decrypt" lr=0x82768E08 r3=... r10=...
   |         |         |        |         |      |              |
   |         |         |        |         |      |              +- guest return address: the caller
   |         |         |        |         |      +- the tag this point was given
   |         |         |        |         +- "entry" or "mark"
   |         |         |        +- the guest address the point overrides
   |         |         +- the calling thread, the log's own `[tNNNN]` id
   |         +- seconds since the first recorded event
   +- sequence number since the last clear
```

The overlay shows the same events newest-twelve, plus:

- the guest's **requested** video mode, the `resolution` preset and the refresh rate;
- the present path (`present_letterbox`, `present_safe_area_x/y`, `present_effect`, dither, overscan);
- the window's physical and logical size, fullscreen and DPI scale;
- the ring's state (held / dropped / total).

### Two standing limits the panel states rather than hides

- **The presenter's own paint rectangle is not exposed by the SDK** — `rex::ui::Presenter` has no
  public accessor for it. The overlay prints `(not exposed by the SDK)` instead of deriving a
  plausible number, because a probe that reports a guess is worse than one that reports nothing. A
  later change can either add an SDK patch that exposes it or read the value the mouse driver already
  measures.
- **A point only exists where a hook does.** Nothing is auto-discovered: `probe_points.cpp` places
  the points, and a new one is one line.

## What is wired today, and why only that

[probe_points.cpp](../../src/diag/probe_points.cpp) carries the four named functions of the MOGG path
— the only guest path this project has proved end to end. A real trace, vanilla
(`--ultimate_mode=0`; the Ultimate run gives the same order and census, and the per-event times vary
by a few tens of milliseconds run to run — the order and the counts are the evidence):

```text
#000001 +    0.000s tF8000028 0x82768C88 entry "mogg.header"
#000002 +    0.015s tF8000028 0x823DE0C0 entry "mogg.hv_decrypt"    lr=0x82768E08
#000003 +    0.015s tF8000028 0x823DE070 entry "mogg.enc_method"    lr=0x823DE0E4
#000004 +    0.056s tF8000028 0x82768AD0 entry "mogg.setup_cypher"  lr=0x82768E48
```

Three facts fall straight out of it, none of which needed a guess:

- `lr=0x82768E08` on `hv_decrypt` is inside `CheckHmxHeader`'s body (`0x82768C88`), which is the
  "only caller" claim in [symbols.md](../symbols.md) measured rather than read;
- `lr=0x823DE0E4` on the first `enc_method` is inside `HvDecrypt` (`0x823DE0C0` + 0x24), i.e. the key
  is selected by the function that installs it;
- the census (1,806 `header` entries against 1 `hv_decrypt` and 1 `setup_cypher` in the same window)
  corrected the rate symbols.md recorded for `CheckHmxHeader` — see that document's MOGG row.

**The menu path is deliberately not wired yet.** A point can only be placed on a function that is
already identified, and the main menu's functions have no names; finding them is exactly the work
[plans/customization-plan.md](../plans/customization-plan.md) S2 does, and the probe is what it does
it with. That is why the plan's P2 verification is "the instrument works on a real path", not "the
title → main menu trace exists" — that trace is S2's output.

## Adding a point

1. Pick a function that is already named, or an address proved by hand
   ([symbols.md](../symbols.md) is the table).
2. Add the override to [probe_points.cpp](../../src/diag/probe_points.cpp), with the guest address in
   the macro and a `subsystem.thing` tag:

   ```cpp
   REX_EXTERN(__imp__sub_XXXXXXXX);
   REX_HOOK_RAW(sub_XXXXXXXX) {
     RBBLITZ_PROBE(0xXXXXXXXX, "menu.screen");
     __imp__sub_XXXXXXXX(ctx, base);
   }
   ```

   The override is a pass-through: the original body is `__imp__<name>`, exactly as
   [ultimate.cpp](../../src/hooks/ultimate.cpp) reasons about its own overrides.
3. Run with `--probe_trace=1 --probe_trace_path=<file>`, and read the file.

**A caution about hot points.** `CheckHmxHeader` is entered ~65 times a second while the title music
plays, so a point on a per-frame or per-packet function will fill a small ring in seconds. That is
what `probe_trace_capacity` is for; it is also why the tag census, not the raw order alone, is the
first thing to read.
