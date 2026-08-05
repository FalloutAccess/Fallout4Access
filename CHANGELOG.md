# Changelog

## v0.1.0 (Jun 13 - Aug 5, 2026)

### New Features
- **No more NVDA controller DLL** — Screen reader support no longer needs `nvdaControllerClient.dll` copied into your game folder. The mod now talks to NVDA directly. If you placed that file there for an earlier version, you can delete it; nothing needs to be installed alongside the mod for speech to work.
- **Speech without a screen reader** — If no screen reader is running, the mod now speaks through your Windows voices (OneCore, or SAPI) instead of staying silent. A screen reader is still recommended and is always preferred when one is running.
- **More screen readers supported** — In addition to NVDA and JAWS, the mod now works with ZoomText, SenseReader, ZDSR, PC-Talker and BoyPCReader.
- **Original (pre-next-gen) Fallout 4 support** — The mod now loads and runs on the original 1.10.163 version of the game, not only the next-gen update. This is the version used by downgraded installs and by mods like Fallout: London. A single download works on both; the right version is detected automatically, so there is nothing to configure.
- **Tested on both Fallout 4 runtimes** — This release is verified working on both the next-gen update (Anniversary Edition) and the original 1.10.163 runtime (used by Fallout: London) from a single install — no separate downloads and nothing to switch.
- **Auto-walk works in Fallout London** — On the original game runtime (used by Fallout: London), pressing *Walk to Object* now walks your character to the selected scanned object, the same as on the regular game. Previously the dispatch into the game's scripts couldn't move the player on this runtime; it now routes through the auto-walk quest's travel package so navigation around obstacles works as expected.
- **Repeat an improvement's details on demand at workbenches** — Pressing *Announce Stats* (H by default) while browsing mods at a weapon, armor, power-armor, or robot workbench now re-reads the highlighted improvement's full characteristics: its stat changes, its description, and its required components. You can hear what a mod does again at any time without scrolling away and back.

### Improvements
- **Loads on every next-gen patch** — The mod is no longer tied to one exact game version. It now loads on any next-gen patch (1.11.137 or later) instead of a single specific build.
- **Starting or restarting your screen reader mid-game now just works** — If you launch Fallout 4 before your screen reader, or your screen reader crashes and restarts during play, the mod now picks it up on its own within a few seconds instead of staying silent (or stuck on a Windows voice) until you restart the game.

### Bug Fixes
- **Auto-aim now works with a controller** — When playing with a gamepad, aiming down the sights no longer failed to lock on. Auto-aim previously only updated while controller input was actively changing, so holding aim with the sticks at rest meant it never noticed you'd raised your sights — no target announcement, no rotation onto the enemy, and no lock-on tone. It now updates every frame regardless of input device, so it behaves the same on controller as it always did on keyboard and mouse.
- **Combat and lock-on sounds no longer crash the game on the original runtime** — On the original 1.10.163 version of Fallout 4 (the version used by Fallout: London), the hit-marker sounds and the auto-aim lock-on tone could crash the game the moment they first played — usually the instant you landed a hit in combat. They now play correctly.
- **Walk to Object no longer traps you in the Pip-Boy** — Pressing *Walk to Object* (Shift+Home by default) inside the Pip-Boy on a page other than the Map would start your character auto-walking while the Pip-Boy stayed open, leaving you unable to act or close it. The object-scanner and auto-walk keys now do nothing inside the Pip-Boy except on the Map page, where they continue to control map markers as before.
- **Fast travel from the Pip-Boy map now works with keyboard selection** — After choosing a destination with the marker navigation keys and *Announce Object* (Home), pressing the fast-travel key would often report there was no marker there and do nothing. It now locks onto the marker you actually selected and opens the normal travel confirmation (or the undiscovered / survival-mode message) regardless of where the mouse cursor happened to be.
- **Workbench mods now tell you what they do** — When browsing improvements at a weapon, armor, power-armor, or robot workbench, the mod's effect — its stat changes and its description — is now announced along with its name and component requirements. Previously the materials needed were read but what the improvement actually does was often left out, because that text isn't ready the instant you land on a mod; the announcement now waits for it to fill in.
- **VATS no longer announces the previous enemy's name** — After killing a target in VATS (or leaving VATS and re-entering) and then aiming at a new enemy, VATS would sometimes speak the *previous* enemy's name instead of the new one — you'd only hear the correct name after re-opening VATS. It now waits for the target display to catch up to the enemy you're actually aiming at, so you hear the right name the first time.

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
