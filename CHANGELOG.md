# Changelog

## v0.1.0 (Jun 13 - Jun 14, 2026)

### New Features
- **Original (pre-next-gen) Fallout 4 support** — The mod now loads and runs on the original 1.10.163 version of the game, not only the next-gen update. This is the version used by downgraded installs and by mods like Fallout: London. A single download works on both; the right version is detected automatically, so there is nothing to configure.

### Improvements
- **Loads on every next-gen patch** — The mod is no longer tied to one exact game version. It now loads on any next-gen patch (1.11.137 or later) instead of a single specific build.

### Bug Fixes
- **Combat and lock-on sounds no longer crash the game on the original runtime** — On the original 1.10.163 version of Fallout 4 (the version used by Fallout: London), the hit-marker sounds and the auto-aim lock-on tone could crash the game the moment they first played — usually the instant you landed a hit in combat. They now play correctly.

## v0.0.3 (Apr 24, 2026)

### New Features
- **Freedom Trail Ring puzzle (Old North Church)** — Looking at the rotate-clockwise, rotate-counter-clockwise, or center activator now announces the letter currently at the pointer. After each rotation, the new letter is announced when the ring settles; activating the center speaks the submitted letter. Blind players can now solve the "RAILROAD" puzzle without sight.
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
- **Committing a marker pans the map and updates the in-game selection** — Pressing *Announce Object* (Home) on the Pip-Boy map now centers the map on the current scanner marker and moves the virtual cursor over it, so the in-game visual selection (and fast travel) follows your keyboard choice. *Next Marker* / *Previous Marker* stay as a fast keyboard-only browse without moving the map.
- **Auto-aim targets uncovered body parts** — When an enemy is partly behind cover, auto-aim no longer wastes shots on the wall. It now checks head, chest, spine, pelvis, shoulders, and thighs individually and aims at whichever body part is actually visible, re-checking every tracking tick so aim follows the target as they peek in and out of cover. The LOS cue also tracks this finer visibility, so the tone plays as soon as *any* part of the enemy is exposed.
- **Workbench mods announce stat changes while you browse** — Scrolling through available mods at a weapon, armor, power-armor, or robot workbench now speaks the numeric delta for each stat the mod affects (e.g. "Long Barrel. Damage +5, Range +20, Weight +2"). The already-installed or "standard" choice naturally yields no deltas and falls back to the previous name-and-description announcement. Cooking recipes are unaffected.
- **Currently-installed mods announced at weapon and armor workbenches** — After committing an item to modify, the list of mods already on it is read aloud (e.g. "Installed: Long Barrel, Reflex Sight, Full Stock"). Weapons and armor with no mods read as "No mods installed". Re-announced if you back out and pick a different item, or after you install/remove a mod.

### Improvements
- **Autowalk no longer aborts on its own.** The previous stuck-detection and combat-entry auto-stop would cut walks short when briefly bumping into a pillar or when a distant enemy engaged — annoying on long traversals across the Commonwealth. Autowalk now only stops on arrival or when you press a cancel key, so you stay in control.

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
