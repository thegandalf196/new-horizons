# New Horizons — focused scripture overrides

This file records narrow, user-approved changes to
[`design-sources/New Horizons.docx`](design-sources/New%20Horizons.docx) without
requiring the complete source document to be replaced.

## Authority and precedence

1. An **Accepted** entry in this file overrides the source document only for the
   exact mechanic, entity, building, spell, hero, value, or presentation rule
   named by that entry.
2. Everything not explicitly changed by an Accepted entry continues to follow
   the source document and the repository contracts in
   [`NEW_HORIZONS_DESIGN.md`](NEW_HORIZONS_DESIGN.md) and
   [`NEW_HORIZONS_MVP.md`](NEW_HORIZONS_MVP.md).
3. If two Accepted entries address the same rule, the later entry supersedes the
   earlier one. Keep the older entry for history and mark it **Superseded**.
4. Draft notes, questions, implementation proposals, and agent inferences have
   no authority. Only instructions confirmed by the user may be marked
   **Accepted**.
5. An override changes the design contract; it is not proof that the change has
   been implemented. Record implementation evidence separately in the entry.

## Entry template

```markdown
### YYYY-MM-DD — Short rule name

- **Status:** Draft | Accepted | Superseded
- **Overrides:** Exact scripture section, entity, or rule.
- **Rule:** Complete replacement rule.
- **Implementation evidence:** Commit/tests, once implemented.
```

## Accepted overrides

### 2026-09-21 — Astral Nexus function

- **Status:** Accepted
- **Overrides:** The unique-building function assigned to Dungeon's Astral Nexus.
- **Rule:** Visiting heroes immediately replenish their spell points to their
  normal maximum.
- **Implementation evidence:** Pending.

### 2026-09-21 — House of Wisdom replaces Magic University

- **Status:** Accepted
- **Overrides:** Conflux's Magic University name and function.
- **Rule:** The building is named **House of Wisdom** and sells randomly
  generated spell scrolls instead of teaching secondary skills.
- **Implementation evidence:** Pending.
