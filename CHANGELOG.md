# Changelog

## v0.0.3 (Mar 2 - Apr 14, 2026)

### New Features
- **MCM (Mod Configuration Menu)** - Full screen reader accessibility for the menu itself: mod headers with page counts, page entries, right-pane option labels with live values (switchers, sliders, steppers, dropdowns, key bindings), hint text, and sub-mode transitions (remap, text input, conflict)
- **Customizable Keybindings via MCM** - All 10 accessibility actions (scan objects, navigate objects and map markers, announce stats, switch menu sides, etc.) are now rebindable to keyboard, mouse, or gamepad inputs
- **BookMenu** - Book and note reading with automatic page turn detection, works with any input method (keyboard, gamepad, remapped controls)
- **Gameplay State Announcer** - Announces camera view changes (first/third person, free camera), sneak enter/exit, and detection state changes while sneaking
- **SleepWaitMenu & SitWaitMenu** - Wait/sleep menu accessibility with mode, hours, and current time announcements
- **Barter Menu Enhancements** - H key announces caps for focused side and pending trade total with direction; trade cost/profit spoken when staging items; quantity picker speaks value changes
- **Container Menu Enhancements** - H key announces carry weight; quantity picker speaks value changes
- **Level and XP Announcement** - H key announces current level and XP % toward next level on the Pip-Boy Stats page and the Perk chart
- **Pip-Boy Miscellaneous Quest Objectives** - Navigate and hear individual Misc objectives in the Quests tab
- **Junk Component Lists** - Component breakdown (e.g., "Components: Gears 2, Screws 3 tagged, Steel") now read aloud in Container, Barter, Pip-Boy, and Examine menus
- **MCM Section Headings** - Right-pane section headings announced when entering a new section

### Bug Fixes
- Fix autowalk self-cancelling when the rebound hotkey shares a key with a cancel action (e.g., Shift+E hotkey vs. Sprint bound to Shift)
- Fix radio signal found/lost notifications not being announced
- Fix announcements stacking on top of stale speech when using MCM-rebound keys (speech now interrupts consistently on user-initiated actions)
- Menu navigation now respects the game's own key remaps instead of hardcoded WASD/defaults

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
