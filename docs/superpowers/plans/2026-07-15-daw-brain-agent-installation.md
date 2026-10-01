# DAW Brain Agent Installation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Install DAW Brain as primary OpenCode agent in the APEX workspace without modifying APEX code.

**Architecture:** Direct file extraction and configuration merge following the user's 12-step specification.

**Tech Stack:** OpenCode agent system, PowerShell commands for file operations.

## Global Constraints

- Workspace: `C:\Users\1993v\OneDrive\Desktop\Apex backup`
- Do not modify APEX source code
- Do not overwrite existing OpenCode configurations (none found)
- Preserve all existing providers, models, API settings
- No functional changes to APEX during this installation
- No refactoring of APEX code
- No Visual Studio project modifications without authorization

---

### Task 1: Workspace Verification and File Discovery

**Files:**
- Verify: `C:\Users\1993v\OneDrive\Desktop\Apex backup\`
- Discover: All required files for installation

**Interfaces:**
- Consumes: User-provided workspace path
- Produces: Confirmed workspace location and file inventory

- [ ] **Step 1: Confirm workspace directory exists**

Run: `Test-Path -LiteralPath "C:\Users\1993v\OneDrive\Desktop\Apex backup"`
Expected: `True`

- [ ] **Step 2: List workspace contents**

Run: `Get-ChildItem -LiteralPath "C:\Users\1993v\OneDrive\Desktop\Apex backup" -Force`
Expected: Shows `.claude`, `Daw Brain`, `My DAW`, `New folder`, `recaps`

- [ ] **Step 3: Locate DAW Brain file**

Run: `Get-ChildItem -LiteralPath "C:\Users\1993v\OneDrive\Desktop\Apex backup" -Recurse -Filter "DAW_BRAIN*.md" | Select-Object FullName`
Expected: `C:\Users\1993v\OneDrive\Desktop\Apex backup\Daw Brain\DAW_BRAIN_v6.0.0-rc.1.md`

- [ ] **Step 4: Locate agent package**

Run: `Get-ChildItem -LiteralPath "C:\Users\1993v\OneDrive\Desktop\Apex backup" -Recurse -Filter "DAW_BRAIN_OpenCode_Agent_Package*" | Select-Object FullName`
Expected: `C:\Users\1993v\OneDrive\Desktop\Apex backup\Daw Brain\DAW_BRAIN_OpenCode_Agent_Package.zip` and extracted folder

- [ ] **Step 5: Check for existing OpenCode config**

Run: `Test-Path -LiteralPath "C:\Users\1993v\OneDrive\Desktop\Apex backup\opencode.json"`
Expected: `False` (no existing config)

---

### Task 2: Brain File Preparation

**Files:**
- Create: `C:\Users\1993v\OneDrive\Desktop\Apex backup\DAW_BRAIN.md`
- Source: `C:\Users\1993v\OneDrive\Desktop\Apex backup\Daw Brain\DAW_BRAIN_v6.0.0-rc.1.md`

**Interfaces:**
- Consumes: Source Brain file
- Produces: Canonical Brain file at workspace root

- [ ] **Step 1: Copy Brain file to root**

Run: `Copy-Item -LiteralPath "C:\Users\1993v\OneDrive\Desktop\Apex backup\Daw Brain\DAW_BRAIN_v6.0.0-rc.1.md" -Destination "C:\Users\1993v\OneDrive\Desktop\Apex backup\DAW_BRAIN.md"`
Expected: File copied successfully

- [ ] **Step 2: Verify file exists and is not empty**

Run: `Get-Item -LiteralPath "C:\Users\1993v\OneDrive\Desktop\Apex backup\DAW_BRAIN.md" | Select-Object FullName, Length`
Expected: `FullName` shows path, `Length` > 0 (should be ~1.9MB)

- [ ] **Step 3: Verify version in file**

Run: `Select-String -LiteralPath "C:\Users\1993v\OneDrive\Desktop\Apex backup\DAW_BRAIN.md" -Pattern "v6.0.0-rc.1" | Select-Object -First 1`
Expected: Match found with version string

- [ ] **Step 4: Verify Master Index exists**

Run: `Select-String -LiteralPath "C:\Users\1993v\OneDrive\Desktop\Apex backup\DAW_BRAIN.md" -Pattern "## Master Index"`
Expected: Match found

- [ ] **Step 5: Verify sections §1–§38 exist**

Run: `Select-String -LiteralPath "C:\Users\1993v\OneDrive\Desktop\Apex backup\DAW_BRAIN.md" -Pattern "^## \d+\." | Measure-Object`
Expected: Count should be 38

---

### Task 3: Agent Package Extraction

**Files:**
- Extract: `C:\Users\1993v\OneDrive\Desktop\Apex backup\Daw Brain\DAW_BRAIN_OpenCode_Agent_Package\*`
- To: `C:\Users\1993v\OneDrive\Desktop\Apex backup\`

**Interfaces:**
- Consumes: Package contents
- Produces: AGENTS.md, opencode.json, .opencode/agents/daw-brain.md at workspace root

- [ ] **Step 1: Create .opencode/agents directory if needed**

Run: `New-Item -ItemType Directory -Path "C:\Users\1993v\OneDrive\Desktop\Apex backup\.opencode\agents" -Force`
Expected: Directory created or already exists

- [ ] **Step 2: Copy AGENTS.md**

Run: `Copy-Item -LiteralPath "C:\Users\1993v\OneDrive\Desktop\Apex backup\Daw Brain\DAW_BRAIN_OpenCode_Agent_Package\AGENTS.md" -Destination "C:\Users\1993v\OneDrive\Desktop\Apex backup\AGENTS.md" -Force`
Expected: File copied

- [ ] **Step 3: Copy opencode.json**

Run: `Copy-Item -LiteralPath "C:\Users\1993v\OneDrive\Desktop\Apex backup\Daw Brain\DAW_BRAIN_OpenCode_Agent_Package\opencode.json" -Destination "C:\Users\1993v\OneDrive\Desktop\Apex backup\opencode.json" -Force`
Expected: File copied

- [ ] **Step 4: Copy daw-brain.md**

Run: `Copy-Item -LiteralPath "C:\Users\1993v\OneDrive\Desktop\Apex backup\Daw Brain\DAW_BRAIN_OpenCode_Agent_Package\.opencode\agents\daw-brain.md" -Destination "C:\Users\1993v\OneDrive\Desktop\Apex backup\.opencode\agents\daw-brain.md" -Force`
Expected: File copied

- [ ] **Step 5: Verify all files exist**

Run: `@("C:\Users\1993v\OneDrive\Desktop\Apex backup\AGENTS.md", "C:\Users\1993v\OneDrive\Desktop\Apex backup\opencode.json", "C:\Users\1993v\OneDrive\Desktop\Apex backup\.opencode\agents\daw-brain.md") | ForEach-Object { Test-Path -LiteralPath $_ }`
Expected: All three return `True`

---

### Task 4: Configuration Validation

**Files:**
- Validate: `C:\Users\1993v\OneDrive\Desktop\Apex backup\opencode.json`

**Interfaces:**
- Consumes: opencode.json file
- Produces: Valid JSON configuration

- [ ] **Step 1: Read opencode.json content**

Run: `Get-Content -LiteralPath "C:\Users\1993v\OneDrive\Desktop\Apex backup\opencode.json" -Raw`
Expected: JSON content with `$schema`, `default_agent`, `instructions`, `share`, `snapshot`

- [ ] **Step 2: Validate JSON syntax**

Run: `Get-Content -LiteralPath "C:\Users\1993v\OneDrive\Desktop\Apex backup\opencode.json" -Raw | ConvertFrom-Json | Select-Object default_agent`
Expected: `default_agent` value is `daw-brain`

- [ ] **Step 3: Verify default_agent setting**

Run: `(Get-Content -LiteralPath "C:\Users\1993v\OneDrive\Desktop\Apex backup\opencode.json" -Raw | ConvertFrom-Json).default_agent`
Expected: `daw-brain`

- [ ] **Step 4: Verify instructions array**

Run: `(Get-Content -LiteralPath "C:\Users\1993v\OneDrive\Desktop\Apex backup\opencode.json" -Raw | ConvertFrom-Json).instructions`
Expected: Array containing `AGENTS.md`

---

### Task 5: Agent Validation

**Files:**
- Validate: `C:\Users\1993v\OneDrive\Desktop\Apex backup\.opencode\agents\daw-brain.md`

**Interfaces:**
- Consumes: Agent definition file
- Produces: Valid agent configuration

- [ ] **Step 1: Read agent frontmatter**

Run: `Get-Content -LiteralPath "C:\Users\1993v\OneDrive\Desktop\Apex backup\.opencode\agents\daw-brain.md" -TotalCount 20`
Expected: Frontmatter with `mode: primary`

- [ ] **Step 2: Verify mode is primary**

Run: `Select-String -LiteralPath "C:\Users\1993v\OneDrive\Desktop\Apex backup\.opencode\agents\daw-brain.md" -Pattern "mode: primary"`
Expected: Match found

- [ ] **Step 3: Verify agent has required permissions**

Run: `Select-String -LiteralPath "C:\Users\1993v\OneDrive\Desktop\Apex backup\.opencode\agents\daw-brain.md" -Pattern "permission:" -Context 0,10`
Expected: Shows `read: allow`, `glob: allow`, `grep: allow`, `edit: ask`, `bash: ask`

- [ ] **Step 4: Verify agent references DAW_BRAIN.md**

Run: `Select-String -LiteralPath "C:\Users\1993v\OneDrive\Desktop\Apex backup\.opencode\agents\daw-brain.md" -Pattern "DAW_BRAIN.md"`
Expected: Multiple matches found

---

### Task 6: AGENTS.md Validation

**Files:**
- Validate: `C:\Users\1993v\OneDrive\Desktop\Apex backup\AGENTS.md`

**Interfaces:**
- Consumes: AGENTS.md file
- Produces: Valid agent rules document

- [ ] **Step 1: Read AGENTS.md content**

Run: `Get-Content -LiteralPath "C:\Users\1993v\OneDrive\Desktop\Apex backup\AGENTS.md" -Raw`
Expected: Content with workspace path, DAW_BRAIN.md reference, project invariants

- [ ] **Step 2: Verify workspace path declaration**

Run: `Select-String -LiteralPath "C:\Users\1993v\OneDrive\Desktop\Apex backup\AGENTS.md" -Pattern "C:\\Users\\1993v\\OneDrive\\Desktop\\Apex backup"`
Expected: Match found

- [ ] **Step 3: Verify DAW_BRAIN.md reference**

Run: `Select-String -LiteralPath "C:\Users\1993v\OneDrive\Desktop\Apex backup\AGENTS.md" -Pattern "DAW_BRAIN.md is the single canonical"`
Expected: Match found

- [ ] **Step 4: Verify realtime audio rules**

Run: `Select-String -LiteralPath "C:\Users\1993v\OneDrive\Desktop\Apex backup\AGENTS.md" -Pattern "No blocking, allocation, file I/O"`
Expected: Match found

---

### Task 7: Visual Studio Project Identification

**Files:**
- Locate: `*.sln` files
- Locate: `*.vcxproj` files

**Interfaces:**
- Consumes: Workspace directory structure
- Produces: Confirmed Visual Studio project location

- [ ] **Step 1: Find main solution file**

Run: `Get-ChildItem -LiteralPath "C:\Users\1993v\OneDrive\Desktop\Apex backup" -Recurse -Filter "DAW_Core.sln" | Select-Object FullName`
Expected: `C:\Users\1993v\OneDrive\Desktop\Apex backup\My DAW\DAW_Core\Builds\VisualStudio2026\DAW_Core.sln`

- [ ] **Step 2: Find main project file**

Run: `Get-ChildItem -LiteralPath "C:\Users\1993v\OneDrive\Desktop\Apex backup" -Recurse -Filter "DAW_Core_App.vcxproj" | Select-Object FullName`
Expected: `C:\Users\1993v\OneDrive\Desktop\Apex backup\My DAW\DAW_Core\Builds\VisualStudio2026\DAW_Core_App.vcxproj`

- [ ] **Step 3: Verify source root exists**

Run: `Test-Path -LiteralPath "C:\Users\1993v\OneDrive\Desktop\Apex backup\My DAW\DAW_Core\Source"`
Expected: `True`

- [ ] **Step 4: Count source subdirectories**

Run: `(Get-ChildItem -LiteralPath "C:\Users\1993v\OneDrive\Desktop\Apex backup\My DAW\DAW_Core\Source" -Directory).Count`
Expected: 96

- [ ] **Step 5: Check for external references in vcxproj**

Run: `Select-String -LiteralPath "C:\Users\1993v\OneDrive\Desktop\Apex backup\My DAW\DAW_Core\Builds\VisualStudio2026\DAW_Core_App.vcxproj" -Pattern "AdditionalIncludeDirectories" | Select-Object -First 1`
Expected: Paths are relative and within workspace

---

### Task 8: Final Validation

**Files:**
- Validate: All installed files
- Validate: Configuration consistency

**Interfaces:**
- Consumes: All previous task outputs
- Produces: Complete validation report

- [ ] **Step 1: Verify workspace is correct**

Run: `Get-Location`
Expected: `C:\Users\1993v\OneDrive\Desktop\Apex backup`

- [ ] **Step 2: Verify DAW_BRAIN.md exists in root**

Run: `Test-Path -LiteralPath "C:\Users\1993v\OneDrive\Desktop\Apex backup\DAW_BRAIN.md"`
Expected: `True`

- [ ] **Step 3: Verify Brain contains v6.0.0-rc.1**

Run: `Select-String -LiteralPath "C:\Users\1993v\OneDrive\Desktop\Apex backup\DAW_BRAIN.md" -Pattern "v6.0.0-rc.1" | Measure-Object`
Expected: Count > 0

- [ ] **Step 4: Verify AGENTS.md exists**

Run: `Test-Path -LiteralPath "C:\Users\1993v\OneDrive\Desktop\Apex backup\AGENTS.md"`
Expected: `True`

- [ ] **Step 5: Verify opencode.json exists and is valid**

Run: `Get-Content -LiteralPath "C:\Users\1993v\OneDrive\Desktop\Apex backup\opencode.json" -Raw | ConvertFrom-Json`
Expected: No error, valid JSON

- [ ] **Step 6: Verify daw-brain.md exists and is primary**

Run: `Select-String -LiteralPath "C:\Users\1993v\OneDrive\Desktop\Apex backup\.opencode\agents\daw-brain.md" -Pattern "mode: primary"`
Expected: Match found

- [ ] **Step 7: Verify no duplicate .opencode directory**

Run: `Get-ChildItem -LiteralPath "C:\Users\1993v\OneDrive\Desktop\Apex backup" -Directory -Filter ".opencode" | Measure-Object`
Expected: Count = 1

- [ ] **Step 8: Report duplicate project copies**

Run: `Get-ChildItem -LiteralPath "C:\Users\1993v\OneDrive\Desktop\Apex backup" -Directory -Filter "My DAW" -Recurse | Select-Object FullName`
Expected: Shows `C:\Users\1993v\OneDrive\Desktop\Apex backup\My DAW` and `C:\Users\1993v\OneDrive\Desktop\Apex backup\New folder\My DAW`

- [ ] **Step 9: Generate final validation table**

Run: Create markdown table with requirements and PASS/BLOCKED status