# APEX Agent Rules

## Canonical knowledge

- `DAW_BRAIN.md` is the single canonical DAW architecture reference.
- `.opencode/agents/daw-brain.md` defines the conversational DAW Brain agent.
- The actual repository and measured runtime determine what APEX currently implements.

## Required workflow

For repository questions:

1. Read the relevant DAW Brain section.
2. Inspect the actual implementation.
3. Identify the authority owner, thread roles, data flow, and lifecycle.
4. Report exact file and symbol evidence.
5. Separate verified facts from architectural recommendations.
6. Do not edit until the user authorizes implementation.
7. After edits, build and run focused regression tests.
8. Never equate compilation with behavioral correctness.

## Project invariants

- No blocking, allocation, file I/O, GUI calls, plugin lifecycle work, or graph rebuilding in the realtime callback.
- Preserve one-owner monitoring paths and prevent duplicate live-input contribution.
- Preserve offline-render isolation from live monitoring and transport-only fades.
- Preserve previous valid project/export artifacts on publication failure.
- Use stable IDs across persistence boundaries.
- Do not invent repository details.
