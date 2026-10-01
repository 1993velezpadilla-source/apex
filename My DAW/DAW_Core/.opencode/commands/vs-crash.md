# /vs-crash

Read the current Visual Studio crash/break context and produce a DAW Brain crash analysis.

## Usage

```
/vs-crash
```

## Behavior

1. Use the `visual_studio` tool with action `crash_context`.
2. Read the resulting crash data (also saved to `.apex-debug/latest-crash.json` and `.apex-debug/latest-crash.md`).
3. Inspect every APEX source file referenced in the call stack.
4. Distinguish the crash location from the original corruption source.
5. Produce the crash analysis in this format:

### Crash
Exception type, message, and active frame.

### Call path
Complete relevant stack.

### Repository evidence
Exact files, symbols, and lines.

### Root-cause analysis
Ownership, lifetime, memory, thread, and state reasoning.

### Smallest safe correction
No unrelated refactor.

### Validation
Build and reproduction steps.

### Remaining uncertainty
Anything not proven.

6. If the `open_source` action is useful, offer to open the crashing source line in Visual Studio.
