# Changelog

## v0.0.2 (Jan 18 - Feb 8, 2026)

### New Features
- **Barter Menu** - Full vendor trading accessibility with side-switching (F key)
- **Cooking, Power Armor, and Robot Workbenches** - Accessible crafting at all workbench types
- **Workshop & Supply Line Menus** - Settlement workshop and supply line management
- **Pip-Boy Map Page** - Marker navigation with mouse detection and autowalk support
- **Pip-Boy STAT & Radio Pages** - Full accessibility for stats and radio station selection
- **Pip-Boy Data Page** - All tab navigation (Quests, Workshops, Stats)
- **H Key Combat Stats** - Press H to hear current combat stats
- **H Key Carry Weight/Caps** - Press H on Pip-Boy inventory to hear carry weight and caps
- **Loading Screen Announcements** - Loading screen text is now read aloud
- **Quick Loot** - Arrow key navigation for the quick loot popup
- **LevelUp & MessageBox Menus** - Accessible level-up perks and message boxes
- **Auto-Aim** - Continuous target tracking for accessibility
- **Quest Info Reading** - Full quest objectives and summary toggle
- **Audio Hit Markers** - Sound feedback for hits, crits, and kills

### Object Scanner Improvements
- Multi-cell scanning with subcategories for items
- Locked door subcategory with door status announcements
- Door destination names and cell doors subcategory
- Corpse loot tracking with unlooted/looted indicators
- Disabled Z-range elevation sorting in exterior cells

### Bug Fixes
- Fix autowalk failing on DLC/mod objects due to float FormID precision loss
- Fix crash when walking to quest markers with null display name
- Fix use-after-free crashes in Pip-Boy callback hook
- Fix Quick Loot RTTI check and use Address Library IDs
- Fix MessageBoxMenu with correct Scaleform paths
- Detect and announce message box content changes without reopening

### Internal
- Migrated DialogueMenu and PipBoy to AdvanceMovie hook pattern
- Replaced BSAudioManager with Papyrus VM for LOS sound playback
- Removed verbose per-frame debug logging
