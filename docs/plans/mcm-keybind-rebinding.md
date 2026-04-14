# MCM Hotkey Rebinding & Game Control Mapping

## Overview

Replace all hardcoded key checks with two systems:
1. **Game control mapping** (`QRawUserEvent()`) for standard UI actions (accept, cancel, navigate)
2. **MCM keybinds** with native Papyrus functions for custom accessibility actions (scan, announce, etc.)

This gives us: key rebinding via MCM, respect for user's in-game control remaps, and gamepad/mouse/keyboard support for free.

## Architecture

### Two-tier input system

```
Standard UI actions (accept, cancel, nav)
  └─> ButtonEvent::QRawUserEvent() == "Accept" / "Cancel" / etc.
      └─> Device-agnostic, respects game remaps

Custom accessibility actions (scan, next object, announce stats)
  └─> MCM detects key press → CallGlobalFunction → our native C++ function
      └─> Device-agnostic, user-rebindable via MCM UI
```

### MCM is a hard dependency

No fallback for MCM-not-installed. MCM handles:
- Key capture / remap UI
- Conflict detection with game controls and other mods
- Persistence (`Data\MCM\Settings\Keybinds.json`)
- Runtime key detection and dispatch to our native functions
- Keyboard + mouse + gamepad unified keycode system

### Key code systems (reference)

| System | Page Up | Numpad 5 | H | Used by |
|--------|---------|----------|---|---------|
| DirectInput scan codes | 0xC9 | 0x4C | 0x23 | MCM keycodes, QIDCode() for keyboard |
| Windows VK / BS_BUTTON_CODE | 0x21 | 0x65 | 0x48 | GetBSButtonCode(), ControlMap |
| User event strings | N/A | N/A | N/A | QRawUserEvent(), ControlMap::GetMappedKey() |

## Implementation Steps

### Phase 1: MCM Config Files

**1.1 Create `Data/MCM/Config/fallout4access/config.json`**

MCM menu layout with a "Keybindings" page containing hotkey controls for each custom action. Structure:

```json
{
  "modName": "fallout4access",
  "displayName": "Fallout 4 Accessibility",
  "pages": [
    {
      "pageDisplayName": "Keybindings",
      "content": [
        {
          "text": "Object Scanner",
          "type": "section"
        },
        {
          "id": "scan_objects",
          "text": "Scan Objects",
          "type": "hotkey",
          "help": "Scan for nearby objects and announce count."
        },
        {
          "id": "next_object",
          "text": "Next Object",
          "type": "hotkey",
          "help": "Select next scanned object."
        }
        // ... etc for all keybinds
      ]
    }
  ]
}
```

**1.2 Create `Data/MCM/Config/fallout4access/keybinds.json`**

Defines what each keybind does. All use `CallGlobalFunction` pointing to our native Papyrus functions:

```json
{
  "modName": "fallout4access",
  "keybinds": [
    {
      "id": "scan_objects",
      "desc": "Scan Objects",
      "action": {
        "type": "CallGlobalFunction",
        "script": "F4A:Keybinds",
        "function": "ScanObjects",
        "params": []
      }
    },
    {
      "id": "next_object",
      "desc": "Next Object",
      "action": {
        "type": "CallGlobalFunction",
        "script": "F4A:Keybinds",
        "function": "NextObject",
        "params": []
      }
    }
    // ... etc
  ]
}
```

Full keybind list (9 actions):

| ID | Description | Default Key | Native Function |
|----|-------------|-------------|-----------------|
| `scan_objects` | Scan nearby objects | Numpad 5 | `ScanObjects()` |
| `next_object` | Select next object | Page Down | `NextObject()` |
| `prev_object` | Select previous object | Page Up | `PrevObject()` |
| `next_category` | Next scanner category | Shift+Page Down | `NextCategory()` |
| `prev_category` | Previous scanner category | Shift+Page Up | `PrevCategory()` |
| `announce_object` | Announce / look at object | Home | `AnnounceObject()` |
| `walk_to_object` | Walk to selected object | Shift+Home | `WalkToObject()` |
| `cycle_subcategory` | Cycle subcategory filter | End | `CycleSubcategory()` |
| `announce_stats` | Announce combat/context stats | H | `AnnounceStats()` |

**1.3 Create `Data/MCM/Config/fallout4access/settings.ini`**

Default keybind assignments (MCM reads defaults from here):

```ini
; No INI settings needed for keybinds - MCM manages keybind defaults
; via the config.json hotkey controls. This file is for future
; non-keybind MCM settings (toggles, sliders, etc.)
```

Note: MCM hotkey defaults are set through the MCM UI or via `SetKeybindEx` Scaleform call. Research whether config.json supports a `defaultKeycode` field, or if we need to register defaults programmatically at game load.

### Phase 2: Native Papyrus Functions

**2.1 Create `Data/Scripts/Source/User/F4A/Keybinds.psc`**

Papyrus script header that declares native functions:

```papyrus
ScriptName F4A:Keybinds

Function ScanObjects() native global
Function NextObject() native global
Function PrevObject() native global
Function NextCategory() native global
Function PrevCategory() native global
Function AnnounceObject() native global
Function WalkToObject() native global
Function CycleSubcategory() native global
Function AnnounceStats() native global
```

**2.2 Create `src/KeybindActions.h` + `src/KeybindActions.cpp`**

C++ implementation that registers native functions and dispatches to existing singletons:

```cpp
// KeybindActions.h
#pragma once

namespace KeybindActions
{
    void RegisterPapyrusFunctions(RE::BSScript::IVirtualMachine* a_vm);
}

// KeybindActions.cpp
#include "KeybindActions.h"
#include "ObjectScanner.h"
#include "AutoWalk.h"
#include "ContextualInfoAnnouncer.h"

namespace KeybindActions
{
    void ScanObjects(RE::StaticFunctionTag*) {
        ObjectScanner::GetSingleton()->ScanAndAnnounce();
    }
    void NextObject(RE::StaticFunctionTag*) {
        ObjectScanner::GetSingleton()->SelectNext();
    }
    // ... etc for all 9 functions

    void RegisterPapyrusFunctions(RE::BSScript::IVirtualMachine* a_vm) {
        a_vm->BindNativeMethod("F4A:Keybinds"_bsfs, "ScanObjects"_bsfs, ScanObjects);
        // ... etc
    }
}
```

**2.3 Register in `main.cpp`**

Add `KeybindActions::RegisterPapyrusFunctions(a_vm)` to the Papyrus registration callback.

**2.4 Thread safety consideration**

MCM calls our native functions via `CallGlobalFunctionNoWait` which runs on the Papyrus thread. If any of our functions need main-thread access (game object manipulation), wrap with `F4SE::GetTaskInterface()->AddTask()`. Test empirically — `SpeechManager::Speak()` and SRAL calls may be fine from any thread.

### Phase 3: Refactor ObjectScanner Input

**3.1 Remove hardcoded key handling from `ObjectScanner::OnButtonEvent()`**

The entire key dispatch in `ObjectScanner.cpp:100-178` gets replaced. MCM now handles all scanner key detection. The only thing that should remain is AutoWalk cancellation on movement keys (WASD/Esc/Space) since those are standard game controls, not custom keybinds.

However, AutoWalk cancel should also use `QRawUserEvent()` instead of hardcoded DInput scan codes:

```cpp
// Before (hardcoded DInput scan codes):
if (idCode == 0x11 || idCode == 0x1E || idCode == 0x1F || idCode == 0x20 ||
    idCode == 0x01 || idCode == 0x39) {

// After (user event based):
auto userEvent = std::string(a_event->QRawUserEvent().c_str());
if (userEvent == "Forward" || userEvent == "StrafeLeft" ||
    userEvent == "StrafeRight" || userEvent == "Back" ||
    userEvent == "Sprint" || userEvent == "Jump") {
```

Note: The exact user event string names need to be verified at runtime. The debug function `DebugDumpQuickContainerMappings()` in `HUDMenuHook.cpp:1312` already tests common event names — use similar approach to discover the correct strings for movement actions.

**3.2 Remove scanner key constants from `ObjectScanner.h`**

Delete the `NUMPAD_5`, `PAGE_UP`, `PAGE_DOWN`, `HOME`, `END_KEY`, `H_KEY` constants (lines 141-146).

**3.3 Evaluate whether ObjectScanner still needs `PlayerInputHandler`**

If all scanner keys are handled by MCM, ObjectScanner may no longer need to be an input handler at all. It would just be a service called by `KeybindActions`. AutoWalk cancellation could move elsewhere (e.g., AutoWalk itself listening for movement events).

### Phase 4: Refactor Menu Input to Use User Events

**4.1 Refactor `MenuAccessibility.cpp` `OnButtonEvent()`**

Replace all hardcoded key comparisons with `QRawUserEvent()` checks. This collapses keyboard + gamepad + mouse into single checks:

```cpp
// Before (3 separate device checks for "accept"):
if (device == kKeyboard) {
    isAcceptKey = (idCode == 0x0D || idCode == 0x45);
} else if (device == kGamepad) {
    isAcceptKey = (idCode == 0x1000);
}

// After (one check, all devices):
auto userEvent = std::string(a_event->QRawUserEvent().c_str());
bool isAcceptKey = (userEvent == "Accept" || userEvent == "Activate");
```

Full mapping for MenuAccessibility:

| Current hardcoded | User event string(s) | Notes |
|---|---|---|
| Up/Down arrows, W/S, DPad Up/Down | Navigation events (TBD exact names) | Need to discover correct event names |
| Left/Right arrows, A/D, DPad L/R | Value change events (TBD) | Context: settings sliders |
| Enter/E, Gamepad A | `"Accept"` / `"Activate"` | |
| Esc/Tab, Gamepad B | `"Cancel"` | Tab may not have a user event — verify |
| Q, Gamepad RT | VATS queue event (TBD) | Context: kVATS |
| Space, Gamepad X | VATS critical event (TBD) | Context: kVATS |

**4.2 Refactor `BarterMenuAccessibility.cpp` and `ContainerMenuAccessibility.cpp`**

- F key (switch sides): This is a custom accessibility action. Move to MCM keybinds OR keep hardcoded if it matches a game control (check if "SwitchFilter" or similar exists).
- H key (announce status): Custom accessibility — goes through MCM `announce_stats` keybind (same function handles context-dependent info).

**4.3 Refactor `HUDMenuHook.cpp`**

- Up/Down arrow hardcoded checks (line 1004) for Quick Loot → use `QRawUserEvent()`.
- Already uses `"QuickContainerUp"` / `"QuickContainerDown"` user events for synthetic events.

### Phase 5: User Event Name Reference

User event names are defined in `CustomControlMap.txt` (game install root). No runtime discovery needed.

**Authoritative source:** `E:\SteamLibrary\steamapps\common\Fallout 4\CustomControlMap.txt`

**Format:** `EventName  KB_VK  Mouse  Gamepad  remappable  linked  unk  contextFlags`

**Key mappings for our refactor:**

Gameplay context (kMainGameplay = 0x0):
| Event | Default KB | Default Gamepad | Our usage |
|-------|-----------|----------------|-----------|
| `Forward` | W (0x57) | Left Stick | AutoWalk cancel |
| `Back` | S (0x53) | Left Stick | AutoWalk cancel |
| `StrafeLeft` | A (0x41) | Left Stick | AutoWalk cancel |
| `StrafeRight` | D (0x44) | Left Stick | AutoWalk cancel |
| `Activate` | E (0x45) | A (0x1000) | Accept in menus |
| `Jump` | Space (0x20) | Y (0x8000) | AutoWalk cancel, VATS critical |
| `Sprint` | LShift (0xA0) | LS (0x0040) | AutoWalk cancel |
| `VATS` | Q (0x51) | LB (0x0100) | VATS queue |
| `Pause` | Esc (0x1B) | Start (0x0010) | AutoWalk cancel |

Menu nav context (kBasicMenuNav = 0x1):
| Event | Default KB | Default Gamepad | Our usage |
|-------|-----------|----------------|-----------|
| `Accept` | Enter (0x0D) | A (0x1000) | Accept/confirm |
| `Cancel` | Tab (0x09) | B (0x2000) | Back/cancel |
| `Up` | Up (0x26) | DPad Up (0x0001) | List nav up |
| `Down` | Down (0x28) | DPad Down (0x0002) | List nav down |
| `Left` | Left (0x25) | DPad Left (0x0004) | Value change left |
| `Right` | Right (0x27) | DPad Right (0x0008) | Value change right |

Page context:
| Event | Default KB | Default Gamepad | Our usage |
|-------|-----------|----------------|-----------|
| `PrevPage` | varies | LT/LB | Pip-Boy tab nav |
| `NextPage` | varies | RT/RB | Pip-Boy tab nav |

VATS context (kVATS = 0xD):
| Event | Default KB | Default Gamepad | Our usage |
|-------|-----------|----------------|-----------|
| `ApplyCritical` | Space (0x20) | X (0x4000) | VATS critical strike |

Quick Container context (kQuickContainerMenu = 0x19):
| Event | Default KB | Default Gamepad |
|-------|-----------|----------------|
| `QuickContainerUp` | Mouse wheel up | DPad Up |
| `QuickContainerDown` | Mouse wheel down | DPad Down |

**Events NOT in ControlMap (accessibility-specific, stay as MCM keybinds):**
- F key for container/barter side switching
- H key for context stats
- All scanner keys (Numpad 5, PgUp/PgDn, Home, End)

**Note:** W/S as menu navigation alternatives are NOT mapped in kBasicMenuNav — only arrow keys and DPad. W/S work because the game also checks `Forward`/`Back` from kMainGameplay in some menu contexts. Verify whether `QRawUserEvent()` populates these in menu mode or only in gameplay mode.

### Suggested Implementation Order

1. Phase 5 (reference only — read CustomControlMap.txt, no coding needed)
2. Phase 1 + 2 (MCM config files + native Papyrus functions)
3. Phase 3 (remove hardcoded scanner keys, MCM handles them)
4. Phase 4 (refactor menu input to use QRawUserEvent)
5. Phase 6 (testing)

### Phase 6: Testing

- [ ] MCM shows fallout4access keybind page
- [ ] Each keybind can be remapped in MCM
- [ ] MCM conflict detection works against game controls
- [ ] Default keys work without user configuration
- [ ] Keyboard keybinds trigger native functions correctly
- [ ] Gamepad buttons can be bound and trigger correctly
- [ ] Mouse buttons can be bound and trigger correctly
- [ ] Menu navigation respects in-game key remaps
- [ ] Accept/Cancel work with remapped keys
- [ ] AutoWalk cancel works with remapped movement keys
- [ ] Latency is acceptable (screen reader response time)
- [ ] Thread safety — no crashes from Papyrus-thread native function calls

## Files Changed

### New files
- `Data/MCM/Config/fallout4access/config.json` — MCM menu layout
- `Data/MCM/Config/fallout4access/keybinds.json` — Keybind definitions
- `Data/Scripts/Source/User/F4A/Keybinds.psc` — Papyrus script header
- `src/KeybindActions.h` — Native function declarations
- `src/KeybindActions.cpp` — Native function implementations

### Modified files
- `src/main.cpp` — Register Papyrus functions
- `src/ObjectScanner.h` — Remove key constants
- `src/ObjectScanner.cpp` — Remove hardcoded key handling, keep as service
- `src/MenuAccessibility.cpp` — Replace idCode checks with QRawUserEvent()
- `src/BarterMenuAccessibility.cpp` — Replace hardcoded F/H key checks
- `src/ContainerMenuAccessibility.cpp` — Replace hardcoded F/H key checks
- `src/HUDMenuHook.cpp` — Replace hardcoded Up/Down checks
- `src/AutoWalk.cpp` — Replace VK code cancel keys with user events

### Possibly removed
- ObjectScanner's `PlayerInputHandler` inheritance (if all keys go through MCM)

## Open Questions

1. **MCM default keybind assignment**: How does MCM set initial default keycodes for hotkey controls? Does `config.json` support a `defaultKeycode` field, or do we need to register defaults via Scaleform `SetKeybindEx` at game load?

2. **W/S in menu context**: W/S are mapped as `Forward`/`Back` in kMainGameplay but NOT in kBasicMenuNav. Does `QRawUserEvent()` still populate `"Forward"`/`"Back"` when a menu is open, or is it empty? If empty, we may need to check both `QRawUserEvent()` and `ControlMap::GetMappedKey("Forward", kKeyboard, kMainGameplay)` for these alternate nav keys.

3. **F key mapping**: F is mapped as `Quickkeys` in gameplay context. In container/barter menus it acts as side-switch. Check if there's a dedicated user event for this in the container input context, or if it should go through MCM keybinds.

4. **Thread safety**: Do `ObjectScanner::ScanAndAnnounce()`, `SpeechManager::Speak()`, and SRAL calls work safely from the Papyrus thread? Test empirically.

5. **ObjectScanner lifecycle**: If ObjectScanner is no longer an input handler, does it still need `Register()` / `ShouldHandleEvent()` / `OnButtonEvent()`? It may simplify to a plain singleton service class.
