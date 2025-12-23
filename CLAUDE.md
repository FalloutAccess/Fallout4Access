# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Fallout 4 accessibility F4SE plugin that provides screen reader support for blind users. Uses SRAL (Screen Reader Abstraction Library) to speak via NVDA, JAWS, or SAPI.

**Current Status:** Minimal working state - SRAL initialization and test message on plugin load. Menu accessibility features need to be implemented.

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
