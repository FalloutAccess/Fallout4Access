# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Fallout 4 accessibility F4SE plugin that provides screen reader support for blind users. Uses SRAL (Screen Reader Abstraction Library) to speak via NVDA, JAWS, or SAPI.

**Current Status:** Main menu, pause menu, settings menu, save/load menus, confirmation dialogs, in-game terminals, and Pip-Boy (Inventory and Data pages) are accessible. Splash screen text is announced. NPC dialogue subtitles are read aloud with speaker names. More menus need to be implemented.

## Build Commands

```bash
xmake build                    # Build the plugin
xmake clean                    # Clean build artifacts
xmake project -k vsxmake       # Generate Visual Studio project
```

Build output: `build/windows/x64/release/fallout4access.dll`

### Installation

The `xmake install-mo2` task is unreliable. Use manual copy:

```bash
cp build/windows/x64/release/fallout4access.dll "E:/Games/ModOrganizer/Fallout 4/mods/fallout4access/F4SE/Plugins/"
```

## Architecture

### Dependencies

- **CommonLibF4** (lib/commonlibf4) - F4SE reverse-engineered API as git submodule
- **SRAL** (lib/SRAL) - Screen reader abstraction library, built as static library with UIA disabled

### Key APIs

- **SpeechManager** - `SpeechManager::GetSingleton()->Speak()` - Centralized speech output with automatic logging
- **SRAL** - `SRAL_Initialize()`, `SRAL_GetCurrentEngine()` (internal use only - use SpeechManager for speech)
- **RE::UI** - UI system access and menu event registration
- **RE::IMenu** - Menu interface with Scaleform movie access
- **RE::Scaleform::GFx** - Flash/Scaleform UI framework for accessing menu elements

## Requirements

- XMake 2.8.2+
- C++23 compiler (MSVC or Clang-CL)
- Windows x64

## Log Files

The plugin creates two log files in `D:\alex\OneDrive\Документы\My Games\Fallout4\F4SE\`:

### fallout4access.log (Main Debug Log)

Contains debug/info logging for development:
- Plugin initialization and SRAL status
- Menu open/close events
- Hook installation details
- Scaleform data access
- Errors and warnings

**Does NOT contain speech output** (that goes to the speech log).

### fallout4access_speech.log (Speech Transcript)

Contains timestamped record of all screen reader output:
```
[12:34:56] Fallout 4 accessibility plugin loaded successfully
[12:34:58] Main Menu
[12:35:01] Continue
[12:35:03] Load
```

**Format:** `[HH:MM:SS] <spoken text>`

**Use this log to:**
- Debug what was announced to the user
- Review gameplay sessions
- Verify speech output is correct
- Check announcement timing and ordering

The log is truncated on each game launch (fresh log per session).

## Workflow Preferences

### Always Use Subagents For:

- **CommonLibF4 API research** - Use `commonlibf4-api-analyst` subagent when exploring RE:: classes, menu systems, Scaleform APIs, or any CommonLibF4 functionality in `lib/commonlibf4`
- **Log analysis** - Use `f4se-log-analyzer` subagent when reading and analyzing plugin logs at `D:\alex\OneDrive\Документы\My Games\Fallout4\F4SE\`:
  - `fallout4access.log` - Main debug log (initialization, menu events, hooks, errors)
  - `fallout4access_speech.log` - Speech-only log (timestamped record of all screen reader output)

These subagents have specialized knowledge and reduce context usage.

### Speech Output (SpeechManager)

All speech output goes through the centralized `SpeechManager` singleton:

```cpp
#include "SpeechManager.h"

// Speak text (logs to fallout4access_speech.log automatically)
SpeechManager::GetSingleton()->Speak("Hello world");

// With interrupt parameter (should always be false per project rules)
SpeechManager::GetSingleton()->Speak(text, false);
```

**Rules:**
- **Never call `SRAL_Speak()` directly** - always use `SpeechManager::GetSingleton()->Speak()`
- **Never use interrupt=true** - always use `interrupt=false` (the default)
- Speech is automatically logged to `fallout4access_speech.log` with timestamps

**Log files:**
- `fallout4access.log` - Debug/info logging (menu events, hooks, errors)
- `fallout4access_speech.log` - Speech transcript with `[HH:MM:SS]` timestamps

### Scaleform UI Discovery

When working with Scaleform UI elements, **do not guess paths**. Instead:

1. Extract SWF files from BA2 archives (e.g., `Fallout4 - Interface.ba2`)
2. Use **JPEXS Free Flash Decompiler (FFDec)** to examine the SWF structure
3. Look at the exported ActionScript to find instance names and text field paths

FFDec can export SWF contents including scripts, symbols, and structure which reveals the exact Scaleform paths needed for `GetVariable()` calls.

**Extracted BA2 archives location:** `E:/Games/modding/Fallout4/fallout4extract-ba/`
- Decompiled SWFs are in `Interface/*-extracted/scripts/` subdirectories

### Known Scaleform Paths (MainMenu.swf)

```
Main menu list:           root.Menu_mc.MainPanel_mc.List_mc
  - selectedIndex         (int) current selection
  - entryList             (array) menu items with .text property

Splash screen text:       root.Menu_mc.SplashScreenHolder_mc.SplashScreenText_tf.text
                          ("$Press any button to start" - localized)

Confirmation dialog:      root.Menu_mc.ConfirmPanel_mc
  - visible               (bool) whether dialog is shown
  - Text path:            root.Menu_mc.ConfirmPanel_mc.Text_mc.TextPulse_mc.textField.text

Save/Load panel:          root.Menu_mc.SaveLoadHolder_mc
  - visible               (bool) whether panel is shown
  - Panel_mc.isSaving     (bool) true=save mode, false=load mode
  - Panel_mc.List_mc      (BSScrollingList) save entries list
    - selectedIndex       (int) current selection
    - entryList           (array) save entries with properties:
      - text              (string) location name
      - name              (string) character/save name
      - playTime          (string) format "days.hours.minutes.seconds"
      - level             (int) player level
      - corrupt           (bool) save is corrupted
      - obsolete          (bool) save is from older version

Settings panel:           root.Menu_mc.SettingsPanel_mc
  - SettingsList_mc       category list (Gameplay, Controls, Display, Audio)
Options panel:            root.Menu_mc.OptionsPanel_mc.Fader_mc.List_mc
Controls panel:           root.Menu_mc.ControlsPanel_mc.Fader_mc.List_mc
```

### Known Scaleform Paths (TerminalMenu.swf)

```
Terminal base:            root.TerminalBase_mc (Terminal class instance)

Text fields (pre-animation strings for immediate access):
  - strHeaderText         (string) terminal name/header
  - strWelcomeText        (string) welcome message
  - strDisplayText        (string) body text or hacking grid

Menu list:                root.TerminalBase_mc.MenuItemList_mc (BSScrollingList)
  - selectedIndex         (int) current selection
  - entryList             (array) menu items with .text property

Hacking mode:
  - HackingAttempts_tf.text (string) "X ATTEMPT(S) LEFT"

Terminal modes (RE::TerminalMenu::Mode):
  - kInit (0), kHack (1), kLogin (2), kList (3)
  - kText (4), kImage (5), kHolotape (6), kWaitingForPapyrus (7)
```

### Known Scaleform Paths (PipboyMenu.swf)

```
Data object:              root.DataObj (central data container populated from C++)

Page/Tab state:
  - CurrentPage           (int) 0=STAT, 1=INV, 2=DATA, 3=MAP, 4=RADIO
  - CurrentTab            (int) sub-tab index within current page

Inventory arrays:
  - InvItems              (array) all inventory items
  - InvSelectedItems      (array) selected index for each tab [7 elements]

Inventory item properties (InvItems[i]):
  - text                  (string) item display name
  - count                 (int) stack count
  - equipState            (int) 0=unequipped, >0=equipped
  - isLegendary           (bool) has legendary effects
  - taggedForSearch       (bool) tagged for component search
  - favorite              (int) 0=not favorite, >0=quickkey slot

Item card details:        root.DataObj.ItemCard_mc.InfoObj (array)
  - text                  (string) stat label (e.g., "Value", "Weight", "DAM")
  - value                 (number/string) stat value
  - damageType            (int) damage type ID
  - showAsDescription     (bool) true if this is description text

Quest data:               root.DataObj.QuestsList (array)
  - text                  (string) quest name
  - description           (string) quest description
  - active                (bool) currently tracked
  - completed             (bool) quest completed
  - isDivider             (bool) is a category divider
  - formID                (uint) quest form ID (0 = misc container)

Workshop data:            root.DataObj.WorkshopsList (array)
  - text                  (string) settlement name
  - happiness             (number) settlement happiness percentage
  - population            (int) number of settlers

General stats:            root.DataObj.GeneralStatsList (array)
  - text                  (string) stat name
  - value                 (number/string) stat value

Pip-Boy pages:
  - STAT (0): Status, SPECIAL, Perks tabs
  - INV (1): Weapons, Apparel, Aid, Misc, Junk, Mods, Ammo tabs
  - DATA (2): Quests, Workshops, Stats tabs
  - MAP (3): World Map, Local Map tabs
  - RADIO (4): Radio stations list
```

### Known Scaleform Paths (HUDMenu.swf)

```
Subtitles:                root.BottomCenterGroup_mc.SubtitleText_mc
  - SpeakerName_tf.text   (string) speaker's name
  - SubtitleText_tf.text  (string) subtitle/dialogue text

HUD Notifications:        root.HUDNotificationsGroup_mc
  - Messages_mc           message display container
    - ShownMessageArray   (array) currently visible messages
    - MessageArray        (array) queued messages
  - QuestUpdates_mc       quest notification area
    - QuestName_tf.text   (string) quest name
    - UpdateType_tf.text  (string) "Started", "Completed", etc.
  - ObjectiveUpdates_mc   objective notification area
    - ShownObjectivesV    (array) visible objectives
  - XPMeter_mc            XP gain display
    - NumberText.text     (string) XP amount
    - LeveUpTextClip.textField.text (string) level up text (note: "LeveUp" typo in game)
  - TutorialText_mc       tutorial popup
    - TutorialText_tf.text (string) tutorial message

Quickloot widget:         root.CenterGroup_mc.QuickContainerWidget_mc
  - _selectedIndex        (int) current selection
  - ListItems_mc          container for item clips
    - ItemText0..N        (MovieClip) individual item entries
      - visible           (bool) whether clip is shown
      - selected          (bool) whether item is selected
      - data              (object) item data with properties:
        - text            (string) item name
        - count           (int) stack count
        - equipState      (int) 0=unequipped, >0=equipped
        - isLegendary     (bool) has legendary effects
        - favorite        (int) quickkey slot
        - taggedForSearch (bool) tagged for component search
  - ListHeaderAndBracket_mc.ContainerName_mc.textField_tf.text (string) container name

Enemy health meter:       root.TopCenterGroup_mc.EnemyHealthMeter_mc
  - DisplayText_tf.text   (string) enemy name
  - LegendaryIcon_mc.visible (bool) is legendary enemy
  - SkullIcon_mc.visible  (bool) is skull (high level) enemy
  - MeterBar_mc.Percent   (number) health percentage
```

### Known Scaleform Paths (ContainerMenu.swf)

```
Side selection:           root.containerIsSelected (bool) true=container, false=player

Container side:           root.ContainerInventory_mc
  - ContainerListHeader   header with container name
    - headerText          (string) container name
    - textField.text      (string) alternative text access
  - ContainerList_mc      (BSScrollingList) container items
    - selectedIndex       (int) current selection
    - entryList           (array) items

Player side:              root.PlayerInventory_mc
  - PlayerListHeader      header with player inventory label
    - headerText          (string) header text
  - PlayerList_mc         (BSScrollingList) player items
    - selectedIndex       (int) current selection
    - entryList           (array) items

Item card:                root.ItemCard_mc
  - InfoObj               (array) stat entries (same format as Pip-Boy)

Sort button:              root.SortButton
  - ButtonText            (string) current sort mode

Pickpocket mode:          root.PickpocketInfo_mc
  - visible               (bool) whether pickpocket UI is shown
  - Percent_tf.text       (string) success chance percentage

Entry properties (entryList[i]):
  - text                  (string) item name
  - count                 (int) stack count
  - equipState            (int) 0=unequipped, >0=equipped
  - isLegendary           (bool) has legendary effects
  - favorite              (int) quickkey slot
  - taggedForSearch       (bool) tagged for component search
```

### Known Scaleform Paths (DialogueMenu.swf)

```
NPC name:                 root.name_tf.text (string)

Dialogue list:            root.List_mc (BSScrollingList)
  - alpha                 (number) visibility (0 when hidden)
  - selectedIndex         (int) current selection
  - entryList             (array) dialogue options

Entry properties (entryList[i]):
  - text                  (string) dialogue option text
  - challengeLevel        (int) speech check difficulty (0=none)
  - challengeResult       (int) success/fail result
  - said                  (bool) already selected this option
  - isBarterOption        (bool) opens barter menu
  - isInventoryOption     (bool) opens inventory
  - endsScene             (bool) ends conversation
```

### Known Scaleform Paths (ExamineMenu.swf)

```
Item name:                root.ItemName_tf.text (string)

Inventory list:           root.InventoryBase_mc.InventoryList_mc
  - selectedIndex         (int) current selection
  - entryList             (array) items to examine

Mod slots:                root.ModSlotBase_mc
  - SlotsLabel_tf.text    (string) "MOD SLOTS" label
  - ModSlotList_mc        (BSScrollingList) available mod slots
    - selectedIndex       (int) current selection
    - entryList           (array) mod slots

Mod description:          root.ModDescriptionBase_mc.ModDescription_tf.text (string)

Item card:                root.ItemCardList_mc.InfoObj (array)
  - Same format as Pip-Boy/Container item cards

Legendary description:    root.LegendaryItemDescription_tf.text (string)

Entry properties for mods (entryList[i]):
  - text                  (string) mod name
  - currentMod            (bool) currently installed
  - hasRequired           (bool) has required materials

Entry properties for items (entryList[i]):
  - text                  (string) item name
  - count                 (int) stack count
  - equipState            (int) equipped state
  - isLegendary           (bool) has legendary effects
  - bestInClass           (bool) best item of this type
```

### Known Scaleform Paths (LevelUpMenu.swf)

```
Perk grid:                root.Menu_mc.GridView_mc (PerkGrid instance)

Selected perk:            root.Menu_mc.GridView_mc.selectedPerkEntry (object)
  - text                  (string) perk name
  - description           (string) perk description
  - displayRank           (int) current viewing rank
  - displayMaxRank        (int) maximum rank available
  - owned                 (bool) player has this perk
  - available             (bool) can be purchased

Selected perk clip:       root.Menu_mc.GridView_mc.selectedPerkClip
  - row                   (int) grid row (0-9 for SPECIAL stats)
  - col                   (int) grid column (rank level)

Player info:
  - root.Menu_mc.playerLevel  (int) current player level
  - root.Menu_mc.perkCount    (int) available perk points

Rank viewing:             root.Menu_mc._ViewingRankOffset (int)
Description text:         root.Menu_mc._SelectionDesc (string)
```

### Known Scaleform Paths (SPECIALMenu.swf)

```
SPECIAL list:             root.Menu_mc.List_mc (BSScrollingList)
  - selectedIndex         (int) current selection (0-6 for S.P.E.C.I.A.L.)
  - entryList             (array) SPECIAL stats

Points counter:           root.Menu_mc.PointCounter_tf.text (string)
Description:              root.Menu_mc.Description_tf.text (string)

Entry properties (entryList[i]):
  - text                  (string) stat name (Strength, Perception, etc.)
  - value                 (int) current stat value (1-10)
```

### Known Scaleform Paths (VATSMenu.swf)

```
Menu instance:            root.MenuInstance (main container)

Selected body part:       root.MenuInstance.SelectedPart (int) index

Part info array:          root.MenuInstance.PartInfos (array)
  Each entry contains:
  - GraphicInstance.TintedContainer.AnimationInstance
    - Name.text           (string) body part name
    - HitChance.text      (string) hit chance percentage
    - PartHealth.BarInstance.scaleX (number) health bar scale

Target resistances:       root.MenuInstance.ResistancesInstance
  - Level.text            (string) target level
```

### Input Key Codes

Fallout 4 uses **Windows Virtual Key codes** (not DirectInput scan codes) for keyboard input in `RE::ButtonEvent::QIDCode()`:

```
Navigation:
  - Up Arrow:    0x26 (VK_UP)
  - Down Arrow:  0x28 (VK_DOWN)
  - Left Arrow:  0x25 (VK_LEFT)
  - Right Arrow: 0x27 (VK_RIGHT)
  - W:           0x57
  - A:           0x41
  - S:           0x53
  - D:           0x44

Actions:
  - Enter:       0x0D (VK_RETURN)
  - E:           0x45 (Activate)
  - Tab:         0x09 (VK_TAB, used as Back in terminals)
  - Escape:      0x1B (VK_ESCAPE)
  - Space:       0x20 (VK_SPACE)

Pip-Boy specific:
  - Q:           0x51 (Previous tab)
  - E:           0x45 (Next tab)
  - 1-5:         0x31-0x35 (Direct page select)

Device types (RE::INPUT_DEVICE):
  - kKeyboard:   0
  - kMouse:      1
  - kGamepad:    2
```

### F4SE Task Interface Warning

**NEVER use infinite/recursive polling with `F4SE::GetTaskInterface()->AddTask()`**. Each task executes on the next frame, so recursive scheduling like this will hang the game:

```cpp
// BAD - causes game hang!
void StartPolling() {
    taskInterface->AddTask([this]() {
        DoSomething();
        StartPolling();  // Infinite recursion floods task queue
    });
}
```

Instead, schedule a **fixed number** of checks:

```cpp
// GOOD - limited checks
void ScheduleDelayedChecks(int count) {
    if (count <= 0) return;
    taskInterface->AddTask([this, count]() {
        DoSomething();
        ScheduleDelayedChecks(count - 1);  // Decrements to zero
    });
}
```

This runs one check per frame for N frames, then stops.

### Logging Guidelines

**Avoid verbose per-frame logging.** Functions called from `AdvanceMovie` hooks or other per-frame callbacks execute 60+ times per second. Adding `REX::DEBUG` calls in these paths causes log files to grow to megabytes in minutes.

**When to use each log level:**

```cpp
// REX::INFO - Use sparingly for significant events:
// - Plugin initialization/shutdown
// - Hook installation success
// - Menu open/close events
// - Feature activation (e.g., "Q key pressed, switching sides")
REX::INFO("ContainerMenuAccessibility: Menu opened");

// REX::ERROR - Use for actual failures that need attention:
REX::ERROR("Failed to install AdvanceMovie hook");

// REX::DEBUG - Avoid in per-frame code paths. OK for:
// - One-time initialization diagnostics
// - Code under active development (remove before commit)
// - Behind a compile-time flag for deep debugging
```

**Never add DEBUG logging to:**
- `AdvanceMovie` hooks or functions called from them
- `GetSelectedIndex()`, `GetItemAtIndex()`, or similar getters
- State-checking functions (`CheckSelection`, `CheckStateChanges`, etc.)
- Any function that runs every frame

**If you need per-frame diagnostics** during development, use a counter to limit output:
```cpp
static int logCount = 0;
if (logCount < 10) {
    REX::DEBUG("Diagnostic info: {}", value);
    logCount++;
}
```
Remove these before committing.
