# Agents and skills — the roster

How a session learns what exists, and when to use which.

## How discovery works

VS Code builds the list an agent sees **automatically**, from these places:

| Source | Injected as |
| --- | --- |
| `.github/agents/*.agent.md` | a custom agent, with its `description`, shown in the agent picker and offered as a subagent |
| `.github/skills/*/SKILL.md` | a skill, with its `name` + `description` (the body loads only when it triggers) |
| the user profile (`<profile>/agents/`, `<profile>/prompts/`) | the same, available in every workspace |
| the extension's built-ins | e.g. the read-only exploration subagent |

So **nobody has to be told about them**: an agent reads its own context, finds the descriptions, and
delegates. This file is the human-readable companion — what each one is for, and what it is not for.
When in doubt, the descriptions in the agent's own context are authoritative for that session (a
customization added mid-session may need a reload before it appears).

## Roster

| Agent | Kind | Tools | Use when | Not for |
| --- | --- | --- | --- | --- |
| `docs-auditor` | this repo (`.github/agents/docs-auditor.agent.md`) | read, search, execute | auditing the docs: the checker's findings, stale or contradictory documents, plan naming/placement, the index | editing anything — it is read-only by design |
| `Explore` (built-in) | extension | read, search | gathering context across the codebase before writing: locating the code a document describes, finding who cites a path | audits with a verdict, or any change |

## Skills

| Skill | Path | Use when |
| --- | --- | --- |
| `documentation` | `.github/skills/documentation/SKILL.md` | writing, moving, renaming, merging, retiring or reviewing anything under `docs/`; updating docs as part of a code change |

Skills are on-demand: the description decides whether the body is loaded, so its trigger words matter.
Slash form: `/documentation`.

## Adding an agent or a skill

1. Agents live in `.github/agents/<name>.agent.md`; skills in `.github/skills/<name>/SKILL.md` (the
   folder name must equal the `name` field).
2. Write the `description` first — it is the entire discovery surface. Start with `Use when:` and the
   phrases somebody would actually say.
3. Keep the tool list minimal: read-only agents get `[read, search]` (plus `execute` only if they must
   run a script), and a body that says what they must not do.
4. Add one row to the roster above, and link any new document from `docs/README.md`.

The full guidance for customization files (frontmatter, locations, anti-patterns) is the
`agent-customization` skill.
