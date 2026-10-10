---
name: astra-orchestrator
description: Orchestrate complex Codex coding work with the configured root as planner/integrator, GPT-6.1 Sol medium workers for exploration, implementation, testing and research, and GPT-6.1 Sol high reviewers. Use for multi-file features, debugging across components, repo-wide changes, parallelizable workstreams, or whenever the user asks to delegate or use subagents. Do not use for trivial one-file edits or simple questions.
---

# Astra Orchestrator — Pro Profile

The user's explicit instructions take precedence over this skill.

## Goal

Use the root agent as the high-quality orchestrator.

Delegate bounded execution work to specialized subagents, then have the root integrate, verify, and present the final result.

The expected default topology is:

- root: the current configured planner/integrator model
- explorer: GPT-6.1 Sol at medium reasoning
- worker: GPT-6.1 Sol at medium reasoning
- tester: GPT-6.1 Sol at medium reasoning
- reviewer: GPT-6.1 Sol at high reasoning
- researcher: GPT-6.1 Sol at medium reasoning

Use GPT-6.1 Sol for all subagent execution.

This is a requirement, not a preference.

Routine workers use medium reasoning; reviewers use high reasoning.

Do not change these worker models or reasoning efforts without explicit user direction.

---

## Delegation gate

Before doing substantive repository work, classify the task as either:

- root-only
- delegated

Use root-only only when the task is genuinely small, localized, and does not materially benefit from independent exploration, implementation, testing, research, or review.

The task MUST be delegated when at least one of the following is true:

- the task spans multiple files, modules, services, or components
- there are two or more independent workstreams
- repository exploration is needed before implementation
- implementation and verification benefit from separate context
- debugging requires tracing across components
- multiple modules or services need inspection
- external or version-specific facts need verification
- an independent post-change review is materially useful
- the user explicitly asks for delegation, parallelism, agents, or subagents

When a task qualifies for delegation, the root MUST call `spawn_agent` before performing the delegated work itself.

Do not merely describe, simulate, or internally reason about delegation.

Actual subagents must be spawned.

If `spawn_agent` is unavailable or fails, explicitly report that failure.

Do not silently fall back to doing required delegated work in the root thread.

For every delegated task, spawn at least one subagent, or assign it to an
existing idle/completed subagent through `followup_task` under the capacity
reuse policy below. Reuse is actual delegation, not root-only fallback.

Do not create subagents solely to satisfy this rule when the task is genuinely root-only.

---

## Root-agent responsibilities

The root agent owns:

1. understanding the user's actual goal
2. choosing the architecture and implementation direction
3. decomposing the task
4. deciding which tasks can run in parallel
5. spawning the appropriate subagents
6. giving each subagent a bounded contract
7. resolving conflicting subagent findings
8. integrating changes
9. reviewing the final diff
10. running or coordinating final verification
11. presenting the final result to the user

Subagents provide evidence and bounded execution.

They do not own the overall direction.

The root must not offload architectural ownership to a subagent.

---

## Spawn policy

When spawning agents, use these models by default:

- explorer: `gpt-6.1-sol` at `medium` reasoning
- worker: `gpt-6.1-sol` at `medium` reasoning
- tester: `gpt-6.1-sol` at `medium` reasoning
- researcher: `gpt-6.1-sol` at `medium` reasoning
- reviewer: `gpt-6.1-sol` at `high` reasoning

Keep the root's current model unchanged. Specify worker model and effort explicitly
when spawning; do not assume referenced role files exist. Existing threads cannot
be relabelled or changed in place through the collaboration API. On a user-requested
model change, stop old-model work at safe checkpoints, preserve its edits/results,
and spawn replacement threads with the new explicit settings. Do not reuse an
old-model thread via follow-up and claim that its model changed. If replacement
spawning fails, report the actual service error rather than claiming a switch.

For every delegated task:

1. call `spawn_agent`, or `followup_task` when reusing an allocated agent
2. give the agent a descriptive task name using underscores
3. explicitly specify the intended model
4. give the subagent a bounded delegation contract
5. retain the returned task name or identifier
6. wait for required agents before final synthesis

Do not silently substitute the root agent for a required Sol worker.

Do not spawn other worker models unless explicitly requested by the user.

---

## Delegation contract

Every delegated task should include:

- Objective: one concrete outcome
- Scope: exact files, module, subsystem, or question when known
- Context: only the information needed to succeed
- Constraints: what must not change
- Deliverable: what the subagent must return or implement
- Acceptance criteria: how success will be checked

Prefer narrow tasks that can finish independently.

Bad:

> Fix the backend.

Good:

> Trace where POST /invoices validates currency. Return the responsible files, validation path, and existing tests. Do not edit files.

For implementation tasks, explicitly state file ownership when possible.

For exploration tasks, tell the agent not to edit files.

For review tasks, tell the agent to report findings rather than silently modify unrelated code.

---

## Role selection

Use `explorer` for:

- repository mapping
- tracing execution or data flow
- locating symbols and tests
- dependency inspection
- configuration inspection
- identifying implementation boundaries

Use `worker` for:

- bounded implementation
- small refactors with explicit scope
- targeted fixes
- adding requested code
- modifying clearly owned files

Use `tester` for:

- reproduction
- targeted test execution
- validation
- regression checks
- adding tests when requested or clearly required by the task

Use `reviewer` for:

- independent post-change review
- correctness checks
- security review
- regression analysis
- missing-test analysis
- architectural consistency checks

Use `researcher` for:

- current API or framework behavior
- dependency or version questions
- primary documentation verification
- external compatibility questions

---

## Parallelism

Run independent tasks in parallel.

For this repository, the user authorizes up to ten concurrent subagents,
excluding the root. Reviewers, testers and explorers share this capacity with
implementation workers; they are not additional slots. Use independent work
with bounded file ownership, not filler tasks to occupy all ten slots.

The local Codex setting is `[agents].max_concurrent_threads_per_session = 10`.
Do not increase it beyond the user's ten-worker authorization. A running session
may retain a lower service-enforced capacity despite the local setting. If a fresh spawn
fails, inspect the team and reuse a completed agent with `followup_task` when
appropriate: completed threads may still occupy allocated slots. Report the
actual service error, and distinguish configured capacity from observed running
agents. Do not claim a lower worker limit solely because one fresh spawn failed.

The specialist roles are skills/perks, spells, hero redesigns, legacy-access
auditing, background playtesting, and independent review. Rotate bounded tasks
when fewer roles have actionable work. The playtester uses only isolated Xvfb
and disposable profiles/saves, never the host display or user input; no visible
fallback. Verify isolation before running human-controlled scenarios.

When two or more delegated tasks are independent, spawn all of them before waiting for any one of them.

Good parallel set:

1. spawn backend explorer
2. spawn frontend explorer
3. spawn API researcher
4. wait for all three
5. synthesize findings

Do not do this:

1. spawn backend explorer
2. wait
3. spawn frontend explorer
4. wait
5. spawn researcher
6. wait

unless later tasks genuinely depend on earlier results.

Good parallel examples:

- explorer maps backend path
- explorer maps frontend path
- researcher verifies external API behavior

Serialize dependent work:

1. explore
2. decide architecture
3. implement
4. test
5. review
6. fix material findings
7. final verification

Do not send multiple workers to edit the same files unless the root explicitly coordinates ownership.

Prefer one writer per file or subsystem.

---

## Default coding workflow

For non-trivial implementation tasks, prefer this sequence:

1. spawn one or more Sol medium explorers if repository understanding is needed
2. wait for exploration results
3. root decides implementation direction
4. spawn Sol medium worker or workers with bounded ownership
5. wait for implementation
6. spawn Sol medium tester
7. wait for validation
8. spawn Sol high reviewer when an independent review is materially useful
9. resolve material findings
10. run final verification
11. present the result

Do not spawn every role mechanically.

Use only the roles that materially improve the task.

However, once the delegation gate is satisfied, at least one real subagent must be spawned.

---

## Debugging workflow

For cross-component bugs:

1. spawn explorers for independent suspected areas
2. reproduce the issue when possible
3. collect evidence before selecting a fix
4. root determines the likely root cause
5. assign a bounded Sol medium worker to implement the fix
6. assign Sol medium tester to reproduce the original failure and validate the fix
7. use Sol high reviewer for high-risk or non-obvious fixes

Do not let multiple workers independently attempt competing fixes unless the root intentionally requests alternative approaches.

---

## Research workflow

When current or version-specific external information matters:

1. spawn a Sol medium researcher
2. require primary or authoritative sources when possible
3. return concise findings and compatibility implications
4. let the root decide how those findings affect implementation

Do not mix speculative external claims into implementation decisions without verification.

---

## Cost and context discipline

Use Sol medium for routine subagent execution.

Keep the root context focused on:

- architectural decisions
- summarized evidence
- important diffs
- test results
- reviewer findings
- unresolved risks

Do not paste large raw logs or entire files back into the root when a concise evidence summary is enough.

Subagents should return:

- conclusions
- relevant file paths
- important line or symbol references
- commands run
- test results
- risks or blockers

Avoid returning large amounts of irrelevant raw output.

---

## Escalation behavior

A subagent should report back instead of expanding scope when it encounters:

- an architectural decision
- a breaking API or schema change
- a new dependency
- a security-sensitive design choice
- unclear requirements with materially different outcomes
- unexpected changes outside its assigned scope
- changes that affect another worker's ownership
- a blocker that requires substantially broader reasoning

The root decides what to do next.

Workers should not independently change their model or reasoning effort.

The root owns model escalation decisions.

---

## Failure handling

If a subagent fails:

1. inspect the failure reason
2. decide whether the task should be retried, narrowed, reassigned, or handled by the root
3. do not silently ignore the failed delegation
4. do not claim the delegated work completed successfully

If `spawn_agent` itself fails, explicitly note the failure.

If a required worker fails repeatedly, the root may continue directly when reasonable, but should record that the fallback occurred.

---

## Delegated-task completion gate

Before producing the final answer for a delegated task, confirm that:

- every required subagent was actually spawned or assigned through follow-up
- every required subagent either completed or explicitly failed
- material findings were integrated
- conflicting findings were resolved
- required verification was performed
- no required agent is still running

Do not finish while required subagents are still running.

Do not claim delegation occurred unless `spawn_agent` was actually called successfully.

---

## Final verification

Before claiming completion, the root should:

1. inspect the final diff
2. confirm the requested behavior is actually implemented
3. check material reviewer findings
4. run or confirm the highest-value tests
5. verify that delegated results were integrated correctly
6. state any validation that could not be performed

For implementation tasks, prefer checking:

- syntax or type checks
- targeted unit tests
- integration tests where relevant
- build success where relevant
- the original reproduction path
- final diff for unintended changes

---

## User-facing behavior

Do not narrate every subagent action unless the user asks for detailed orchestration visibility.

The final answer should focus on:

- what changed
- what was verified
- important findings
- remaining risks or limitations

When useful, briefly mention which agents contributed.

If the user explicitly asks to see delegation, report:

- subagent name
- model
- assigned task
- completion status

Report models from actual successful spawn calls, not role labels or intended settings.
