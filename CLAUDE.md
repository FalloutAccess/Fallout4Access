# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Fallout 4 accessibility F4SE plugin that provides screen reader support for blind users. Uses SRAL (Screen Reader Abstraction Library) to speak via NVDA, JAWS, or SAPI.

**Current Status:** Main menu, pause menu, settings menu, save/load menus, and confirmation dialogs are accessible. Splash screen text is announced. More menus need to be implemented.

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

- **SRAL** - `SRAL_Initialize()`, `SRAL_Speak()`, `SRAL_GetCurrentEngine()`
- **RE::UI** - UI system access and menu event registration
- **RE::IMenu** - Menu interface with Scaleform movie access
- **RE::Scaleform::GFx** - Flash/Scaleform UI framework for accessing menu elements

## Requirements

- XMake 2.8.2+
- C++23 compiler (MSVC or Clang-CL)
- Windows x64

## Workflow Preferences

### Always Use Subagents For:

- **CommonLibF4 API research** - Use `commonlibf4-api-analyst` subagent when exploring RE:: classes, menu systems, Scaleform APIs, or any CommonLibF4 functionality in `lib/commonlibf4`
- **Log analysis** - Use `f4se-log-analyzer` subagent when reading and analyzing `fallout4access.log` or other F4SE plugin logs at `D:\alex\OneDrive\Документы\My Games\Fallout4\F4SE\`

These subagents have specialized knowledge and reduce context usage.

### SRAL Usage

- **Never use interrupt=true** with `SRAL_Speak()` calls. Always use `interrupt=false` (the default in SpeakText).

### Scaleform UI Discovery

When working with Scaleform UI elements, **do not guess paths**. Instead:

1. Extract SWF files from BA2 archives (e.g., `Fallout4 - Interface.ba2`)
2. Use **JPEXS Free Flash Decompiler (FFDec)** to examine the SWF structure
3. Look at the exported ActionScript to find instance names and text field paths

FFDec can export SWF contents including scripts, symbols, and structure which reveals the exact Scaleform paths needed for `GetVariable()` calls.

**Extracted BA2 archives location:** `E:/Games/modding/fallout4extract-ba/`
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
