# DAW Brain Canonical Install Package

## Candidate identity

`DAW_BRAIN_AUDITED_INTEGRATION_CANDIDATE.md`

SHA-256:

`ce92abd24f6ed246004047a374cbf791dedc8fe5b2c2ee98d7154cfa6bc9a7ca`

## Install

From PowerShell, with this package and the APEX repository available:

```powershell
.\INSTALL_DAW_BRAIN_AUDITED_CANONICAL.ps1 -RepoRoot "C:\path\to\APEX"
```

The installer:

- validates the candidate SHA-256;
- backs up the current `DAW_BRAIN.md`;
- installs the audited candidate;
- verifies the installed hash;
- writes `DAW_BRAIN.install_manifest.json`.

## Verify

```powershell
.\VERIFY_DAW_BRAIN_AUDITED_CANONICAL.ps1 -RepoRoot "C:\path\to\APEX"
```

## Roll back

```powershell
.\ROLLBACK_DAW_BRAIN_AUDITED_CANONICAL.ps1 -RepoRoot "C:\path\to\APEX"
```

The rollback script restores the newest package-created backup unless a specific backup path is supplied.

## After installation

Give OpenCode:

`OPENCODE_POST_INSTALL_REPOSITORY_VERIFICATION_PROMPT.md`

That phase verifies the **actual repository** separately from the Brain knowledge audit.

## Hard boundary

`CANONICAL_INSTALLED != CURRENT_APEX_REPOSITORY_VERIFIED`
