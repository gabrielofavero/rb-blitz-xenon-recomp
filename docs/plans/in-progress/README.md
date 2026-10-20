# In-progress plans

Plans that are actively being worked. A plan moves here from [`../todo/`](../todo/) when work starts
and leaves for [`../finished/`](../finished/) when it is delivered — one `git mv`, keeping its
`YYYY-MM-DD-` prefix.

One plan was filed here: [`2026-10-17-gameplay-input-mapping-plan.md`](../finished/2026-10-17-gameplay-input-mapping-plan.md) —
General vs Gameplay Mapping: two independent per-input-type switches (Controller, Keyboard + Mouse),
each with its own action table and source kinds, plus source gates and the controller-only
native-preset bypass. It moved to `finished/` on 2026-10-19: P0-P4, P7, P8a and P8b all landed,
and P5/P6 were cancelled by the 2026-10-18 change of course.

Now in progress: [`2026-10-19-interface-gameplay-mapping-restructure-plan.md`](2026-10-19-interface-gameplay-mapping-restructure-plan.md) —
Interface Mapping versus always-on Gameplay Mapping, preset-aware controller defaults, inferred
Custom, the 3→1 listening countdown, row-level binding text, and new Mouse & Keyboard defaults.
**P1 landed 2026-10-19** (the binding rows draw their inputs); P2–P5 are not started.

The in-game settings program outgrew the plan format and lives with its subject; its live tracker is
[`../../settings/status.md`](../../settings/status.md).

See [`../README.md`](../README.md) for the status taxonomy.
