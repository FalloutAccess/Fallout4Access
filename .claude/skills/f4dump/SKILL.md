---
name: f4dump
description: Dump decoded records from a Fallout 4 ESP/ESM file using F4Dump (xEdit)
---

# F4Dump — Decode ESP/ESM Records

Dump decoded record contents from a Fallout: 4 plugin file using F4Dump (xEdit CLI).

**Tool location**: See `CLAUDE.local.md` for the xDump path (run with `-f4` flag for Fallout: 4)

## Arguments

The user provides arguments after `/f4dump`. Parse them as:

```
/f4dump <esp_path> [options]
```

- `<esp_path>` — Path to the ESP/ESM file (required). Can be:
  - A full path: `E:\path\to\mod.esp`
  - Just a filename: `MyMod.esp` — search MO2 mods directory for it
- `[options]` — Optional filters (see below)

## Common Options

| Option | Effect |
|--------|--------|
| `-dg:SCPT` | Dump only scripts |
| `-dg:QUST` | Dump only quests |
| `-dg:GLOB` | Dump only globals |
| `-dg:MESG` | Dump only messages |
| `-dg:FLST` | Dump only form lists |
| `-xbloat` | Exclude large records (LAND, REGN, PGRD, etc.) |
| `-top:N` | Only dump first N records |
| `-check` | Check for errors instead of dumping |

Multiple groups: `-dg:SCPT,QUST,GLOB`

## Steps

1. **Locate the ESP file**:
   - If a full path is given, use it directly
   - If just a filename, search under the MO2 mods directory (see `CLAUDE.local.md`) recursively
   - Also check the game root's `Data\` directory (see `CLAUDE.local.md`)

2. **Determine the data path** (`-d` flag):
   - Set `-d` to the directory containing the ESP file so masters can be found
   - If masters aren't in the same directory, the dump may show unresolved FormIDs (this is usually fine for reading scripts)

3. **Run xDump**:
   ```bash
   "<xDump_path>" -fo4 -q [options] -d:"<esp_directory>" "<esp_path>" 2>&1
   ```
   - Always use `-fo4` to select Fallout: 4 mode
   - Always use `-q` to suppress the version banner
   - Timeout: 30 seconds (large ESMs may take longer)

4. **Handle output**:
   - If output is small (< 5000 lines), display relevant sections directly
   - If output is large, save to `docs/` or a temp file and read the interesting parts
   - For script dumps: focus on the `SCTX - Script Source:` sections (the actual source code)
   - Skip `SCDA - Compiled Script:` hex blobs and `Local Variables` sections unless requested

## Default Behavior

If no options are specified, default to dumping scripts:
```
/f4dump MyMod.esp  →  runs with -dg:SCPT
```

## Examples

```
/f4dump "The Mod Configuration Menu.esp"
  → Dumps scripts from MCM (searches MO2 mods dir)

/f4dump "The Mod Configuration Menu.esp" -dg:SCPT,QUST,GLOB
  → Dumps scripts, quests, and globals

/f4dump Fallout4.esm -dg:MESG -top:20
  → First 20 message records from the base game

/f4dump E:\path\to\custom.esp -check
  → Check the plugin for errors
```

## Error Handling

- **"No inputfile was specified"**: ESP path not found, check the path
- **Masters not found**: Add `-d` pointing to game Data directory. For MO2-managed mods, masters may be in different mod folders — warn the user
- **Timeout**: Large ESMs (Fallout4.esm) with no filters will be huge. Suggest adding `-dg:` or `-xbloat` filters
