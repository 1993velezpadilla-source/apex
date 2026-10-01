# /vs-build

Build APEX in Visual Studio using the VS Control Bridge.

## Usage

```
/vs-build
/vs-build Release x64
/vs-build Debug x64
```

## Behavior

1. Use the `visual_studio` tool with action `build`.
2. Pass configuration and platform from the arguments (default: Release x64).
3. Return the full build result including success/failure, failed project count, elapsed time, and Build Output.
4. If the build fails, show the relevant error lines from Build Output.
