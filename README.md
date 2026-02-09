# Fallout 4 Access

An F4SE plugin that makes Fallout 4 accessible to blind and visually impaired players. Uses [SRAL](https://github.com/m1maker/SRAL) (Screen Reader Abstraction Library) to provide screen reader support through NVDA, JAWS, or SAPI.

## Features

### Accessible Menus

- **Main Menu, Pause Menu** - Full navigation and announcements
- **Settings Menu** - All settings categories with values read aloud
- **Save/Load Menus** - Save slot details including character level and playtime
- **Pip-Boy** - Inventory, Stats, Data (Quests/Workshops/Stats), Map, and Radio pages
- **Container/Looting** - Item lists with stats, weight, and value
- **Barter Menu** - Vendor trading with side-switching (F key)
- **Terminal Menus** - In-game computer terminals
- **Dialogue Menu** - NPC conversation options with speaker names
- **VATS** - Target selection and body part announcements
- **Level-Up Menu** - Perk selection and descriptions
- **Workbenches** - Cooking, Power Armor, and Robot workbenches
- **Workshop & Supply Lines** - Settlement building and caravan management
- **Message Boxes & Confirmation Dialogs** - Prompts and choices
- **S.P.E.C.I.A.L. Menu** - Character creation stat allocation
- **Examine Menu** - Item inspection
- **Loading Screens** - Loading screen text read aloud

### Gameplay Accessibility

- **Object Scanner** - Scans nearby objects by category (NPCs, items, doors, containers) with subcategories, locked door detection, and corpse loot tracking
- **Crosshair Feedback** - Announces what you're looking at
- **Auto-Aim** - Continuous target tracking assistance
- **Auto-Walk** - Navigate to map markers and scanned objects
- **Audio Hit Markers** - Distinct sounds for hits, critical hits, and kills
- **HUD Notifications** - Item pickups, location discoveries, and XP gains read aloud
- **Combat Stats (H key)** - Press H to hear health, ammo, and other combat stats
- **Contextual Info** - Carry weight and caps on Pip-Boy inventory

## Requirements

- Fallout 4 (Steam version)
- [F4SE](https://f4se.silverlock.org/) (Fallout 4 Script Extender)
- A screen reader: [NVDA](https://www.nvaccess.org/) (recommended), JAWS, or Windows SAPI
- [Address Library for F4SE](https://www.nexusmods.com/fallout4/mods/47327)

## Installation

1. Install F4SE and Address Library if you haven't already
2. Copy the `fallout4access` folder into your Fallout 4 Data directory (or install via a mod manager)
3. The folder structure should be:
   ```
   Data/
     F4SE/
       Plugins/
         fallout4access.dll
     Sound/
       FX/
         F4A/
           UI/
             (sound files)
   ```
4. Load `fallout4access.esp` in your mod load order

## Building from Source

### Requirements

- [XMake](https://xmake.io) 2.8.2+
- C++23 compiler (MSVC or Clang-CL)
- Windows x64

### Clone

```bash
git clone --recurse-submodules https://git.alex19ep.me/alex19EP/f4access
cd fallout4access
```

### Build

```bash
xmake build
```

Build output is in `build/windows/x64/release/fallout4access.dll`.

### Install to Mod Organizer 2

Set the environment variable `XSE_FO4_MODS_PATH` to your MO2 mods directory, then:

```bash
xmake install
```

### Generate Visual Studio Project (Optional)

```bash
xmake project -k vsxmake
```

## License

This project is licensed under the [GNU General Public License v3.0](LICENSE).

## Credits

- [CommonLibF4](https://github.com/alandtse/CommonLibF4) - Reverse-engineered F4SE API
- [SRAL](https://github.com/m1maker/SRAL) - Screen Reader Abstraction Library
- [F4SE](https://f4se.silverlock.org/) - Fallout 4 Script Extender
