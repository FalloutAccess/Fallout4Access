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
- **RE::UIUtils** - `RE::UIUtils::PlayMenuSound("UIMenuOK")` - Play UI sounds. See [docs/audio-playback-api.md](docs/audio-playback-api.md)
- **RE::UI** - UI system access and menu event registration
- **RE::IMenu** - Menu interface with Scaleform movie access
- **RE::Scaleform::GFx** - Flash/Scaleform UI framework for accessing menu elements

## Requirements

- XMake 2.8.2+
- C++23 compiler (MSVC or Clang-CL)
- Windows x64

## Log Files

The plugin creates two log files in the standard F4SE log directory (`<Documents>\My Games\Fallout4\F4SE\`). See [local-paths memory file](memory note: local-paths.md) for the actual path on this machine.

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

### Changelog Maintenance

`CHANGELOG.md` is a **user-facing** document for blind Fallout 4 players, not a developer log. Update it as part of every commit that introduces a user-visible change.

**Workflow:**
1. Before finalizing a commit, decide whether the change is user-visible (see criteria below).
2. If yes, check whether the topmost version header in `CHANGELOG.md` is already tagged in git (`git tag --list vX.Y.Z`):
   - **Not tagged yet** → the version is still in-progress. Append new entries under its existing `### New Features` / `### Bug Fixes` / `### Improvements` subsections and update the end date in the version header (e.g., `## v0.0.3 (Mar 2 - Apr 21, 2026)` → bump to today's date).
   - **Already tagged** → the version is frozen. **Ask the user** what the next version number should be before writing anything, then create a new `## vX.Y.Z (today)` section above it.
3. Never use an `## Unreleased` header — always use the concrete `## vX.Y.Z (date range)` form.
4. Stage `CHANGELOG.md` alongside the code changes in the same commit.

**User-visible (goes in changelog):**
- New menus or features the user interacts with
- Bug fixes that change in-game behavior the user would notice
- Changes to announcements, speech, sounds, or keybindings
- Performance or responsiveness improvements users would feel

**Internal (do NOT add to changelog):**
- Refactors, code style, clang-format/tidy changes
- Build system, tooling, hooks, CI
- Submodule updates (unless they fix a user-facing bug — describe the user effect, not the submodule bump)
- Fixing build warnings or compiler errors
- Test-only changes

**Describe the user effect, not the implementation.** Bad: "Centralize `StopSpeech()` calls in dispatcher." Good: "Fix announcements stacking on top of stale speech when using rebound keys."

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

// Stop any in-flight speech (used by input dispatchers, see policy below)
SpeechManager::GetSingleton()->StopSpeech();
```

**Rules:**
- **Never call `SRAL_Speak()` / `SRAL_StopSpeech()` directly** — always go through `SpeechManager`.
- **Don't pass `interrupt=true` to `Speak()` from per-feature code.** Interruption is now centralized — see the policy below. The parameter still exists for the rare callsite that needs to interrupt independently of any input event.
- Speech is automatically logged to `fallout4access_speech.log` with timestamps.

#### Speech interruption policy

Users can rebind our actions to arbitrary keys via MCM. Screen readers (NVDA/JAWS) only auto-stop speech on a fixed key set (Ctrl, arrows), so when a user remaps "next object" to e.g. F2 their announcements would stack on top of stale ones. To handle this we stop speech at the dispatcher level, not at individual `Speak()` callsites:

- **`KeybindActions::Dispatch`** calls `StopSpeech()` once at the top — every MCM-bound action interrupts.
- **`MenuAccessibility::OnButtonEvent`** calls `StopSpeech()` for navigation/value-change keys (Up/Down/Left/Right/Forward/Back/StrafeLeft/StrafeRight). **Not** for Accept/Cancel — those are confirmations, not selection churn.
- Pip-Boy callbacks (`onInvItemSelection`, `onQuestSelection`) and per-frame Scaleform polling fire 1–2 frames after the input handler runs, so their `Speak()` calls naturally play after the dispatcher's `StopSpeech()`. No callsite changes needed.
- **Don't add `interrupt=true` at new `Speak()` callsites.** If you find yourself reaching for it, the trigger is probably an input event — handle it in the dispatcher instead.

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

**Extracted BA2 archives location:** See [local-paths memory file](memory note: local-paths.md) for the path on this machine.
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
