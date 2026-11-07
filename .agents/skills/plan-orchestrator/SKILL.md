---
name: plan-orchestrator
description: Execute a multi-step implementation plan or numbered prompt sequence in this repository through delivery, coordinating independent work and recording validation and plan status as steps finish.
---

# Plan orchestrator

Use when the user supplies a plan or asks to execute all of an existing plan's prompts. Deliver the whole authorized scope, not just the next prompt. The supplied plan and the user's latest instructions define the work; read the applicable `AGENTS.md` and the repository documentation skill before changing project docs.

## Execute

1. Find the owning plan in `docs/plans/` or use the plan supplied in chat. For a substantial plan with no repository document, create a dated plan in `docs/plans/in-progress/` so step notes have a home. Inspect the relevant code, current build state, plan dependencies, and working-tree changes. Preserve unrelated work. If a repository plan is still in `todo/`, move it to `in-progress/` when implementation starts, following `.github/skills/documentation/SKILL.md`.
2. Identify deliverables and acceptance checks for every step. Execute dependent steps in order. Delegate self-contained steps or bounded investigations to subagents when that helps context or elapsed time; run them concurrently only when their files, assumptions, and prerequisites do not conflict. Give each agent the exact scope, relevant constraints, and expected handoff. Integrate and review their results before relying on them. Keep responsibility for the complete plan and final validation.
3. After each step is implemented and checked, update its owning plan with dated notes: what changed, where, which build or tests ran and their results, and what remains unverified. Keep the plan's existing IDs and evidence style. Update related documentation when the code changes its claims. Do not mark a step complete on a subagent report alone.
4. Validate code with focused inspection, a current release build (`cmake --build --preset win-amd64-release`), and relevant automated tests. For Windows payload test executables, use the repository's `rb_blitz_windows_test_runner.exe` or the CTest registrations that already use it. Resolve failures in the changed scope, then rerun the affected checks. Record exact failures and limits that remain.
5. The owner performs the final game/UI test. Do not navigate or automate the game UI for acceptance unless the owner later authorizes it. Do not claim UI behavior was observed from code, a build, or a host test. Build the latest code before handing it over for owner testing.
6. When all implementation steps and available code/build/test validation are done, record the final evidence and any owner UI checks as explicitly unverified. Move the plan from `docs/plans/in-progress/` to `docs/plans/finished/` (the repository's done state), update its indexes and references using the documentation skill, and run its checker. If a required implementation or automated gate remains incomplete, leave the plan in `in-progress/` with the blocker and next action stated.

In the handoff, summarize delivered steps, build and test results, documentation status, and the exact UI checks left to the owner. Follow repository approval rules for any operation outside this workflow.
