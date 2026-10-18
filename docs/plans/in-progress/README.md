# In-progress plans

Plans that are actively being worked. A plan moves here from [`../todo/`](../todo/) when work starts
and leaves for [`../finished/`](../finished/) when it is delivered — one `git mv`, keeping its
`YYYY-MM-DD-` prefix.

One plan is filed here: [`2026-10-17-gameplay-input-mapping-plan.md`](./2026-10-17-gameplay-input-mapping-plan.md) —
General vs Gameplay Mapping: two independent per-input-type switches (Controller, Keyboard + Mouse),
each with its own action table and source kinds, plus source gates and the controller-only
native-preset bypass. It is **in progress**: P0-P4 landed (2026-10-18; §10-§13, §15). P5-P6 were
cancelled by the 2026-10-18 change of course; the remaining work is P7 (the in-game Controls menu,
replacing the native category) then P8 (executable retirement).

The in-game settings program outgrew the plan format and lives with its subject; its live tracker is
[`../../settings/status.md`](../../settings/status.md).

See [`../README.md`](../README.md) for the status taxonomy.
