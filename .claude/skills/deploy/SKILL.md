---
name: deploy
description: Build the plugin and install to MO2 for testing
disable-model-invocation: true
---

# Build and Deploy to MO2

Build the fallout4access plugin and deploy it to Mod Organizer 2 for testing.

## Steps

1. **Build the plugin**:
   ```bash
   xmake build
   ```
   Report any compilation errors. Stop if build fails.

2. **Install to MO2**:
   ```bash
   xmake install
   ```
   This copies the DLL, PDB, and Data files to the MO2 mods folder.

3. **Report status**:
   - Confirm build succeeded
   - Confirm installation completed
   - Remind user: "Launch Fallout 4 through Mod Organizer 2 to test"

## On Failure

If the build fails, analyze the compiler errors and suggest fixes. Common issues:
- Missing includes
- Syntax errors
- CommonLibF4 API changes
- Prism linkage issues (missing /WHOLEARCHIVE, /LTCG or the vendored prism_dep_*.lib)
