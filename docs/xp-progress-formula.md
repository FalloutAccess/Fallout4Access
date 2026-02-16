# Fallout 4 XP Progress Formula

## Overview

The loading screen displays player level and XP progress toward the next level.

## How We Get XP Progress

We read from `BGSSaveLoadManager::queuedEntryToLoad`, which contains the save file
being loaded. Two fields are used:

- `levelProgress` - Current XP accumulated toward the next level (raw value)
- `levelThreshold` - Total XP needed to reach the next level

The percentage is calculated as:
```cpp
float percent = (levelProgress / levelThreshold) * 100.0f;
```

## Game's XP Formula (Reference)

The game uses this formula to calculate XP needed per level:

```
XP needed for level N = iXPBumpBase * (N - 1) + 200
```

Where `iXPBumpBase` is a game setting (default: 75).

Examples:
- Level 1 -> 2: 200 XP
- Level 2 -> 3: 275 XP (200 + 75)
- Level 3 -> 4: 350 XP (200 + 150)
- Level 10 -> 11: 875 XP (200 + 675)

## API Used

- `RE::BGSSaveLoadManager::GetSingleton()` - Save/load manager singleton
- `BGSSaveLoadManager::queuedEntryToLoad` - The save file queued for loading
- `BGSSaveLoadFileEntry::levelProgress` - Current XP toward next level (float)
- `BGSSaveLoadFileEntry::levelThreshold` - XP needed for next level (float)
