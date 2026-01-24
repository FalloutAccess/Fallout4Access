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

Use the `/deploy` skill to build and install in one step, or manually:

```bash
xmake build && xmake install-mo2
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

The log is truncated on each game launch (fresh log per session).

## Workflow

### Skills (Slash Commands)

| Skill | Purpose |
|-------|---------|
| `/deploy` | Build plugin and install to MO2 |
| `/check-logs` | Quick analysis of F4SE logs for errors |
| `/commit` | Create a git commit |

### Subagents

Use these specialized agents for complex tasks:

| Agent | Model | Use For |
|-------|-------|---------|
| `commonlibf4-api-analyst` | opus | RE:: classes, menu systems, Scaleform APIs |
| `fallout4-ui-explorer` | opus | SWF structure analysis, finding UI paths |
| `ghidra-reverse-engineer` | opus | Binary analysis, finding undocumented functions |
| `f4se-log-analyzer` | haiku | Quick log diagnostics |
| `fo4-strings-searcher` | haiku | Localization string lookups |
| `menu-accessibility-reviewer` | sonnet | Code review for accessibility implementations |

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

## Common Mistakes

### 1. Direct SRAL Calls
```cpp
// BAD - bypasses logging and centralized control
SRAL_Speak(text.c_str(), false);

// GOOD
SpeechManager::GetSingleton()->Speak(text);
```

### 2. Logging in Per-Frame Code
```cpp
// BAD - floods log file at 60+ entries/second
void OnAdvanceMovie() {
    REX::DEBUG("Frame update");  // NEVER do this
}

// GOOD - log only on state changes
void OnAdvanceMovie() {
    if (selectionChanged) {
        REX::INFO("Selection changed to {}", newIndex);
    }
}
```

### 3. Infinite Task Recursion
```cpp
// BAD - hangs the game
void Poll() {
    taskInterface->AddTask([this]() {
        DoWork();
        Poll();  // Infinite loop!
    });
}

// GOOD - bounded iterations
void Poll(int remaining) {
    if (remaining <= 0) return;
    taskInterface->AddTask([this, remaining]() {
        DoWork();
        Poll(remaining - 1);
    });
}
```

### 4. Wrong Scaleform Array Access
```cpp
// BAD - GetArraySize is the correct method
int count = entryList.GetMember("length");

// GOOD
std::uint32_t count;
entryList.GetArraySize(&count);
```

### 5. Unchecked GetVariable Returns
```cpp
// BAD - crashes if path doesn't exist
RE::Scaleform::GFx::Value val;
movie->GetVariable(&val, "root.Missing.Path");
std::string text = val.GetString();  // Crash if val is undefined

// GOOD
RE::Scaleform::GFx::Value val;
if (movie->GetVariable(&val, "root.Path") && val.IsString()) {
    std::string text = val.GetString();
}
```

### 6. Forgetting to Check Empty Lists
```cpp
// BAD - crashes on empty list
auto item = GetItemAtIndex(0);
SpeakItem(item);

// GOOD
if (GetListSize() > 0) {
    auto item = GetItemAtIndex(0);
    SpeakItem(item);
}
```

## Scaleform UI Discovery

When working with Scaleform UI elements, **do not guess paths**. Instead:

1. Extract SWF files from BA2 archives (e.g., `Fallout4 - Interface.ba2`)
2. Use **JPEXS Free Flash Decompiler (FFDec)** to examine the SWF structure
3. Look at the exported ActionScript to find instance names and text field paths

FFDec can export SWF contents including scripts, symbols, and structure which reveals the exact Scaleform paths needed for `GetVariable()` calls.

**Extracted BA2 archives location:** `E:/Games/modding/Fallout4/fallout4extract-ba/`
- Decompiled SWFs are in `Interface/*-extracted/scripts/` subdirectories

**Known Scaleform paths:** See [docs/scaleform-paths.md](docs/scaleform-paths.md) for comprehensive path documentation for all implemented menus.

## F4SE Task Interface Warning

**NEVER use infinite/recursive polling with `F4SE::GetTaskInterface()->AddTask()`**. Each task executes on the next frame, so recursive scheduling will hang the game. See "Common Mistakes" section above.

## Logging Guidelines

**Avoid verbose per-frame logging.** Functions called from `AdvanceMovie` hooks or other per-frame callbacks execute 60+ times per second.

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
```

**Never add DEBUG logging to:**
- `AdvanceMovie` hooks or functions called from them
- `GetSelectedIndex()`, `GetItemAtIndex()`, or similar getters
- State-checking functions (`CheckSelection`, `CheckStateChanges`, etc.)
- Any function that runs every frame
