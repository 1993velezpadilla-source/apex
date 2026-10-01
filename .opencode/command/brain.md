---
description: Switch between DAW Brain versions (v6 or v7)
---

# Brain Version Selector

This command manages which DAW Brain version is active for the current workspace.

## Usage

```
/brain          - Show available Brain versions and current active version
/brain v6       - Switch to DAW Brain v6
/brain v7       - Switch to DAW Brain v7
```

## Instructions

When this command is invoked:

1. **If no arguments are provided ($ARGUMENTS is empty):**
   - Read the current `opencode.json` file in the workspace root
   - Identify the current `default_agent` value
   - Display the available Brain versions:
     - `v6` - DAW Brain v6.0.0-rc.1 (frozen §1–§38)
     - `v7` - DAW Brain v7.0.0-alpha.1 (§§1–38 + v7 expansion §§39–42)
   - Show which version is currently active based on the default_agent
   - If default_agent is `daw-brain-v6`, show "Active Brain: v6"
   - If default_agent is `daw-brain-v7`, show "Active Brain: v7"
   - If default_agent is `daw-brain` or something else, show "Active Brain: v6 (default)"

2. **If argument is "v6":**
   - Edit the `opencode.json` file in the workspace root
   - Change the `default_agent` field to `daw-brain-v6`
   - Preserve all other fields in opencode.json
   - Display: "Switched to DAW Brain v6"
   - Remind the user to restart OpenCode for changes to take effect

3. **If argument is "v7":**
   - Edit the `opencode.json` file in the workspace root
   - Change the `default_agent` field to `daw-brain-v7`
   - Preserve all other fields in opencode.json
   - Display: "Switched to DAW Brain v7"
   - Remind the user to restart OpenCode for changes to take effect

4. **If argument is anything else:**
   - Display: "Unknown Brain version. Available versions: v6, v7"

## Important Notes

- Both Brain files remain unchanged and available at all times
- Only the agent reference is updated, not the Brain files themselves
- The user must restart OpenCode after switching Brain versions
- The original `daw-brain.md` agent is preserved for backward compatibility