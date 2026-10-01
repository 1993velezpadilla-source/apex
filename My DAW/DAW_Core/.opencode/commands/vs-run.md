# /vs-run

Build and run APEX in Visual Studio with debugging.

## Usage

```
/vs-run
/vs-run Release x64
```

## Behavior

1. First build APEX using the `visual_studio` tool with action `build` (default: Release x64).
2. If the build succeeds, start debugging with action `start_debug`.
3. Monitor Visual Studio status until APEX exits or enters Break Mode.
4. If an exception is detected (Break Mode with exception):
   - Automatically call `crash_context` to collect full crash data.
   - Read `.apex-debug/latest-crash.json`.
   - Inspect every referenced APEX source location.
   - Produce a crash analysis following the DAW Brain crash analysis format.
5. Report the final state (exited normally, crashed, etc.).
