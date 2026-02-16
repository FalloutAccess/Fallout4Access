# Porting fallout4access to Fallout: New Vegas — Feasibility Analysis

## Executive Summary

**Difficulty: Very High. Possible but essentially a ground-up rewrite.**

The core challenge is not incremental API differences — it's that the two games use **fundamentally different UI technologies**. Fallout 4 uses Scaleform GFx (Flash/SWF with ActionScript 3), while New Vegas uses a **proprietary XML tile system** inherited from Gamebryo. This means the entire approach to reading UI text must be redesigned from scratch.

---

## 1. The UI System Gap (Critical Blocker)

This is the single most important finding.

| Aspect | Fallout 4 | Fallout: New Vegas |
|--------|-----------|-------------------|
| **UI Tech** | Scaleform GFx (Flash/SWF) | Gamebryo XML tiles |
| **Scripting** | ActionScript 3 | No scripting — computed traits via XML operators |
| **Text Access** | `movie->GetVariable(&val, "root.Menu_mc.List_mc")` | `GetUIString("MenuName/TileName/.../string")` |
| **Data Binding** | GFx::Value (String, Int, Array, Object) | Tile::Value (float `num` or `char* str`) |
| **Menu Files** | `.swf` (compiled Flash) | `.xml` (plain text) |
| **Per-Frame Hook** | `AdvanceMovie` (Scaleform movie update) | `kMessage_MainGameLoop` or manual vtable hooks |

The plugin's architecture is built around Scaleform access patterns — `GetVariable()`, `GetMember()`, `GetArraySize()`, `GetElement()` — none of which exist in New Vegas. Every single menu handler (~50+ files) would need to be rewritten to use the tile path system instead.

**What this means practically:**
- In FO4, you do: `movie->GetVariable(&val, "root.ContainerList_mc")` then `val.GetElement(index, &item)` then `item.GetMember("text", &name)`
- In FNV, you'd do: `GetUIString("ContainerMenu/list_window/*:3/string")` or traverse `Tile` children programmatically via `InterfaceManager`

The FNV approach is less powerful (flat string/float values only, no nested objects or arrays), which means more reverse engineering to reconstruct structured data from individual tile traits.

### Bethesda UI Technology Timeline

| Game | Engine | UI Technology | Scaleform? |
|------|--------|---------------|------------|
| Oblivion (2006) | Gamebryo | Proprietary XML tiles | No |
| Fallout 3 (2008) | Gamebryo | XML tiles | No |
| **Fallout: New Vegas (2010)** | **Gamebryo** | **XML tiles** | **No** |
| Skyrim (2011) | Creation Engine | Scaleform (AS 2.0) | Yes |
| Fallout 4 (2015) | Creation Engine | Scaleform (AS 3) | Yes |

---

## 2. Script Extender Differences

| Aspect | F4SE / CommonLibF4 | xNVSE |
|--------|-------------------|-------|
| **Architecture** | 64-bit | 32-bit |
| **C++ Standard** | C++23 | Older (VS 2022 compatible) |
| **Game Class Library** | CommonLibF4 (separate git submodule) | Bundled in xNVSE source (GameTiles.h, GameForms.h, etc.) |
| **Vtable Hooking** | `REL::HookVFT<>` (type-safe templates) | Manual x86 ASM hooks, `__fastcall` thiscall emulation |
| **Menu Access** | `RE::UI::GetMenu("ContainerMenu")` -> `IMenu*` with `.movie` | `InterfaceManager::GetSingleton()` -> `Menu*` with `.tile` |
| **Task Scheduling** | `F4SE::GetTaskInterface()->AddTask()` | `kMessage_MainGameLoop` callback (every frame) |
| **Events** | `RE::BSTEventSink<MenuOpenCloseEvent>` | `NVSEEventManagerInterface` + JIP menu event handlers |

The plugin initialization, messaging, and event systems are all different. `main.cpp` would need a complete rewrite. The convenient `REL::HookVFT<>` templated hooking doesn't exist — you'd need raw address-based patching with x86 assembly knowledge.

There is **no standalone CommonLib equivalent** for New Vegas. Instead, xNVSE bundles reverse-engineered game class headers directly in its source (GameTiles.h, GameForms.h, GameData.h, etc.). Plugins include these headers and link against xNVSE's source files.

---

## 3. What IS Portable

Some components transfer with minimal changes:

| Component | Portability | Notes |
|-----------|------------|-------|
| **SpeechManager** | High | Pure abstraction over SRAL — just needs 32-bit build |
| **SRAL library** | Medium | Needs 32-bit build; alternatively use Tolk which has proven 32-bit support |
| **ObjectScanner** (spatial scanning) | Medium | Core ray-casting/distance logic ports; game class APIs differ |
| **ItemStatFormatter** | Medium | Formatting logic ports; item stat fields differ |
| **AutoWalk** | Medium | Movement input injection concept ports; implementation differs |
| **DeferredItemAnnouncer** | High | Pure announcement queueing logic |
| **LocalizationHelper** | Low | FNV uses different localization system |

---

## 4. 32-bit Constraints

New Vegas is a 32-bit application limited to ~4GB RAM (with the 4GB patcher). This adds:

- **Memory pressure** from loading screen reader DLLs
- **All plugin DLLs must be 32-bit** compiled
- SRAL would need a 32-bit build (or switch to Tolk/UniversalSpeech which have 32-bit binaries)
- No C++23 features that depend on 64-bit-only compiler intrinsics

---

## 5. Menu Event Detection

Detecting when menus open/close is feasible but different:

**FO4 approach:** `RE::BSTEventSink<MenuOpenCloseEvent>` — clean event sink pattern.

**FNV approach:** Two options:
- **JIP LN NVSE** provides `SetOnMenuOpenEventHandler` / `SetOnMenuCloseEventHandler` at script level
- **C++ plugins** can use `NVSEEventManagerInterface` or hook `InterfaceManager` functions directly
- `kMessage_MainGameLoop` provides a per-frame callback for state polling

The event infrastructure exists but is less ergonomic than FO4's typed event sinks.

---

## 6. Reading UI Text in New Vegas

NVSE provides functions to read tile values:

```
GetUIFloat "MenuName/tile/path/trait"   -> float value
SetUIStringEx "MenuName/tile/path/string" newValue  -> sets string
```

At C++ plugin level, the approach would be:
1. Get `InterfaceManager::GetSingleton()`
2. Get menu via `GetMenuByType(menuID)` -> `Menu*`
3. Traverse `menu->tile` children
4. Read `tile->GetValue(traitID)` -> `Tile::Value*` -> `.str` or `.num`

This is less convenient than Scaleform's `GetVariable()` with dot-path strings. You'd need to reverse-engineer each menu's XML tile hierarchy (which is at least readable since the files are plain XML in `Data\Menus\`).

### Key source files in xNVSE for tile access:
- `nvse/nvse/GameTiles.h` — Tile class, Tile::Value, GetValue/SetString/SetFloat
- `nvse/nvse/GameAPI.h` — InterfaceManager singleton
- `nvse/nvse/GameUI.h` — Menu base class

---

## 7. No Existing Precedent

There are **no known screen reader or blind accessibility mods** for Fallout: New Vegas. This would be pioneering work entirely — no existing code to reference, no community knowledge base for this specific use case.

---

## 8. Advantages of the FNV Target

Despite the challenges, FNV does have some advantages:

- **XML menus are human-readable** — Unlike FO4's compiled SWF files that need FFDec to decompile, FNV's menu XML files can be read directly in any text editor. Mapping tile paths is straightforward.
- **Active modding community** — xNVSE is actively maintained. JIP LN NVSE adds 1000+ functions. The xNVSE Discord is responsive.
- **yUI-NVSE** is an existing NVSE plugin focused on UI features and could serve as a reference implementation for C++ tile manipulation.
- **Simpler menus** — FNV menus are structurally simpler than FO4's Flash-based menus.

---

## 9. Effort Breakdown

### What you'd be building:

| Task | Effort | Reuse from FO4 |
|------|--------|----------------|
| Plugin initialization (xNVSE) | Full rewrite | ~0% — different API entirely |
| Menu detection & dispatch | Major rewrite | ~20% — design pattern reusable |
| All menu text extraction (15+ menus) | Full rewrite | ~0% — XML tiles vs Scaleform |
| Speech output (SpeechManager + SRAL) | Minor adaptation | ~90% — 32-bit build needed |
| Input handling | Major rewrite | ~30% — key concepts transfer |
| Object scanner | Medium rewrite | ~50% — spatial logic transfers |
| Dialogue/subtitle hooks | Full rewrite | ~10% — different dialogue system |
| Vtable hooks | Full rewrite | ~0% — x86 ASM vs REL::HookVFT |

### Rough estimate of what transfers:
- **~15-20% of code** would be directly reusable (SpeechManager, announcement patterns, some utility logic)
- **~80-85%** would need to be rewritten from scratch

### Prerequisites for the developer:
- Strong C++ and x86 ASM knowledge
- Familiarity with Gamebryo XML tile internals
- Ability to reverse-engineer game functions via Ghidra/IDA (32-bit)
- Access to the xNVSE community (Discord) for support

---

## 10. Recommended Path Forward

If pursuing this, the most practical approach would be:

1. **Start a new xNVSE plugin project** from the [example plugin](https://github.com/xNVSE/NVSE/blob/master/nvse_plugin_example/main.cpp)
2. **Get SRAL or Tolk working** as a 32-bit DLL
3. **Port SpeechManager** as the first component
4. **Implement one simple menu** (e.g., main menu) as a proof of concept using XML tile access
5. **Study yUI-NVSE and JIP LN NVSE** source code for patterns on tile manipulation
6. Iterate from there, using fallout4access as a design reference

---

## 11. Feature Requirements for FNV Access

This section maps every user-facing feature in fallout4access to its FNV equivalent, noting what applies directly, what needs adaptation, and what is FO4-only.

### Gameplay Features

| F4A Feature | FNV Equivalent | Applies? | Notes |
|-------------|---------------|----------|-------|
| **Object Scanner** (Numpad 5, PgUp/PgDn, Home, End) — scans nearby objects by category (NPCs, doors, containers, items, furniture, quest markers, locations, companions, power armor) with distance and elevation | Same concept, different game classes | Yes | Core spatial logic ports. Power Armor category becomes FNV-specific categories. Game object classes (TESObjectREFR, Actor) exist in xNVSE but with different APIs. FNV has no companions wheel — companion detection needs adaptation. |
| **Auto-Walk** (Shift+Home) — automatically walks player to scanned object | Same concept | Yes | Movement input injection differs. FNV player movement APIs are in xNVSE GameObjects. |
| **Crosshair Targeting** — announces what's under the crosshair | Same concept | Yes | Crosshair reference access exists in xNVSE. `GetCrosshairRef` is a known NVSE function. |
| **Combat Stats** (H key) — announces health, AP, rads, weapon, ammo | Same concept, different stats | Yes | FNV has health, AP, rads, DT/DR instead of just DR. Weapon condition is a stat FO4 doesn't have. Ammo types are more complex in FNV. |
| **Auto-Aim** — snaps to nearest hostile when aiming down sights | Same concept | Yes | Iron sights exist in FNV. Target acquisition logic ports. Need to hook ADS enter/exit differently. |
| **Corpse Loot Tracker** — marks looted corpses, persists across saves | Same concept | Yes | xNVSE has serialization interface for cosave data. |

### Main Menu & System Menus

| F4A Feature | FNV Equivalent | Applies? | Notes |
|-------------|---------------|----------|-------|
| **Main Menu Navigation** — announces menu items as you navigate | Same concept | Yes | FNV main menu is `StartMenu` in XML. Items accessed via tile hierarchy instead of Scaleform list. |
| **Splash Screen Text** — reads "Please Stand By" text | Similar | Yes | FNV has its own intro sequence and splash. |
| **Confirmation Dialogs** — reads "Are you sure?" prompts and button labels | Same concept | Yes | FNV uses `MessageMenu` (menu type 1001). Text in tile `string` trait. |
| **Settings Menu** — navigate settings, announce name and current value | Same concept, different settings | Yes | FNV settings are in XML-based settings menu. Different setting names and categories. No "advanced" settings submenu. |
| **Save/Load Menu** — lists saves with character name and playtime | Same concept | Yes | FNV save menu is a different XML menu but same information is available. |

### Inventory & Pip-Boy

| F4A Feature | FNV Equivalent | Applies? | Notes |
|-------------|---------------|----------|-------|
| **Pip-Boy Inventory Page** — lists items by category with name, qty, weight, value | Same concept (Pip-Boy 3000A) | Yes | FNV Pip-Boy is `InventoryMenu` (XML). Different categories. Item condition is a new stat. Items have different properties (ammo types, weapon mods differ from FO4). |
| **Pip-Boy Data Page** — quests, perks, S.P.E.C.I.A.L., holotapes | Same concept | Yes | FNV `StatsMenu` handles perks/traits/reputation. Quest log is separate. FNV has faction reputation system not in FO4. |
| **Pip-Boy Stats Page** — character level, HP, AP, carry weight | Same concept | Yes | FNV stats include Skills (absent in FO4), DT, reputation. Different stat set. |
| **Pip-Boy Radio Page** — tune radio stations | Same concept | Yes | FNV radio is part of Pip-Boy. Fewer stations typically. |
| **Pip-Boy Map Page** — navigate map markers with scanner-style keys (PgUp/PgDn/Home/End) | Same concept | Yes | FNV map is `MapMenu` (XML). Location data accessible differently. Fast travel works similarly. |
| **Carry Weight & Caps** (H key on inventory) — quick status check | Same concept | Yes | FNV has carry weight and caps. Accessed through different game APIs. |

### Container & Looting

| F4A Feature | FNV Equivalent | Applies? | Notes |
|-------------|---------------|----------|-------|
| **Container Menu** — navigate items, see name/qty/weight, take/store items | Same concept | Yes | FNV `ContainerMenu` is XML-based. Item list is a tile listbox. Filter system may differ. |
| **Pickpocket Detection Chance** — announces success % when pickpocketing | Same concept | Yes | FNV pickpocket uses detection chance. Displayed differently in UI. |
| **Side Switching** (F key) — toggle between container and player inventory | Same concept | Yes | FNV container has two-panel layout in XML. |

### Trading

| F4A Feature | FNV Equivalent | Applies? | Notes |
|-------------|---------------|----------|-------|
| **Barter Menu** — vendor name, item lists, values, caps, filters | Same concept | Yes | FNV `BarterMenu` is XML-based. Barter skill affects prices (FO4 has Charisma perks instead). |

### Dialogue & Conversation

| F4A Feature | FNV Equivalent | Applies? | Notes |
|-------------|---------------|----------|-------|
| **Dialogue Menu** — navigate and select dialogue options | Same but structurally different | Yes | FNV dialogue is a **list of full sentences** (not FO4's 4-option wheel). `DialogMenu` is XML. May actually be simpler to implement since the full text is always shown. |
| **NPC Subtitles with Speaker Names** — reads spoken dialogue with who's speaking | Same concept | Yes | FNV has subtitle display. Speaker identification needs similar hooking approach. |
| **Skill Checks in Dialogue** — announces [Speech 50/75] type checks | Same concept, more prominent | Yes | FNV uses skill checks extensively ([Speech 50], [Barter 30], etc.). These are displayed inline in dialogue text. Should be straightforward to parse. |

### Terminals

| F4A Feature | FNV Equivalent | Applies? | Notes |
|-------------|---------------|----------|-------|
| **Terminal Menu** — read terminal text, navigate options | Same concept | Yes | FNV `TerminalHackingMenu` and `ComputersMenu` are XML-based. Hacking minigame is the same word-guessing mechanic. Terminal text entries work similarly. |

### Combat (VATS)

| F4A Feature | FNV Equivalent | Applies? | Notes |
|-------------|---------------|----------|-------|
| **VATS Target Selection** — announces target name and selected body part | Same concept | Yes | FNV VATS works the same way. `VATSMenu` is XML-based. Body part selection, hit chance display all exist. |
| **VATS Queue & Critical** — announces queued shots and critical meter | Partially different | Partial | FNV has no critical meter (that's FO4-only). Queued shots and AP cost per shot exist. |

### Leveling & Character

| F4A Feature | FNV Equivalent | Applies? | Notes |
|-------------|---------------|----------|-------|
| **Level Up S.P.E.C.I.A.L.** — attribute selection at character creation | Same concept | Yes | FNV S.P.E.C.I.A.L. selection at Doc Mitchell's house. |
| **Perk Selection** — browse and choose perks with descriptions | Same concept, different system | Yes | FNV level-up offers skill points AND perks (every 2 levels). FO4 has perks only. The perk browser UI differs. FNV also has **Traits** at character creation. |

### Workbenches & Crafting

| F4A Feature | FNV Equivalent | Applies? | Notes |
|-------------|---------------|----------|-------|
| **Weapon Workbench** — modify weapons with mod selection | Different system | Partial | FNV weapon modding is simpler (attach/remove fixed mods, no multi-slot system). Uses `RecipeMenu`. |
| **Armor Workbench** — modify armor | N/A | No | FNV has no armor modding workbench. |
| **Chemistry Station** — craft chems/drugs | Different | Partial | FNV has campfire crafting and workbench crafting via `RecipeMenu`. Different recipe system. |
| **Cooking Station** — cook food | Different | Partial | FNV uses campfire for cooking (Honest Hearts DLC expanded this). Same `RecipeMenu`. |
| **Robot Workbench** — build/modify robots | N/A | No | FO4 Automatron DLC only. No equivalent in FNV. |

### Workshop & Settlement

| F4A Feature | FNV Equivalent | Applies? | Notes |
|-------------|---------------|----------|-------|
| **Workshop Building Menu** — browse categories, place objects, see requirements | N/A | No | FO4-only feature. FNV has no settlement building system. |
| **Workshop Stats** (H key) — population, happiness, food, water, power | N/A | No | FO4-only. |
| **Supply Line / Caravan Menu** — manage settlement connections | N/A | No | FO4-only. |

### Notifications & Messages

| F4A Feature | FNV Equivalent | Applies? | Notes |
|-------------|---------------|----------|-------|
| **HUD Messages** — item pickups, location discoveries, XP gains | Same concept | Yes | FNV has corner messages for pickups, XP, location discovery. Hooking mechanism differs. |
| **HUD Notifications** — toast-style alerts (quest updates, perk gained) | Same concept | Yes | FNV has notification banners. |
| **Message Box Dialogs** — system Yes/No prompts | Same concept | Yes | FNV `MessageMenu` with button options. |
| **Loading Screen Tips** — read tip text during loading | Same concept | Yes | FNV has loading screens with tips and lore. `LoadingMenu` is XML-based. |

### FNV-Only Features (Not in FO4, New for FNV Access)

These features would need to be **newly developed** for FNV since they don't exist in FO4:

| Feature | Description | Priority |
|---------|-------------|----------|
| **Skill Point Allocation** | FNV level-up includes distributing skill points across 13 skills. No FO4 equivalent. | High |
| **Trait Selection** | Character creation includes choosing 2 optional traits with tradeoffs. | High |
| **Reputation System** | Faction reputation (Liked, Idolized, Vilified, etc.) displayed in Pip-Boy. No FO4 equivalent. | Medium |
| **Companion Wheel** | FNV companion management uses a radial wheel menu. Different from FO4's dialogue-based companion commands. | Medium |
| **Caravan Card Game** | FNV-specific card minigame played with NPCs. Entirely unique UI. | Low |
| **Casino Gambling** | Blackjack, roulette, slots at casinos. Unique UIs per game. | Low |
| **Hardcore Mode HUD** | Hunger, thirst, sleep meters. No FO4 equivalent (Survival mode in FO4 doesn't show meters the same way). | Medium |
| **Weapon Condition** | Weapons and armor degrade with use. Condition is shown in inventory. FO4 removed this system. | Medium |
| **Ammo Types** | FNV supports multiple ammo subtypes per weapon (AP, HP, JHP, etc.). Selection UI needed. | Medium |
| **Implant Menu** | New Vegas Medical Clinic sells stat-boosting implants. Custom transaction UI. | Low |
| **Challenge Tracking** | FNV has in-game challenges (kill X enemies with Y weapon). Tracked in Pip-Boy. | Low |

### Feature Priority Summary for FNV Access

**Phase 1 — Core (Minimum Viable):**
1. Plugin initialization and speech output (SpeechManager + SRAL/Tolk 32-bit)
2. Main menu navigation
3. Confirmation dialogs / message boxes
4. Pip-Boy inventory with item names and stats
5. Container menu (looting)
6. Dialogue menu with full sentence options
7. HUD messages (pickups, discoveries, XP)
8. Loading screen tips

**Phase 2 — Extended Menus:**
9. Save/Load menu
10. Settings menu
11. Barter menu
12. Terminal menu (text entries + hacking)
13. VATS targeting
14. Pip-Boy map with marker navigation
15. Pip-Boy stats and data pages
16. Level-up (skill points + perk selection)

**Phase 3 — Gameplay Features:**
17. Object scanner with spatial awareness
18. Auto-walk to scanned objects
19. Crosshair targeting announcements
20. Combat stats (H key)
21. Auto-aim assistance
22. NPC subtitle reading with speaker names
23. Corpse loot tracking

**Phase 4 — FNV-Specific:**
24. Skill point allocation at level-up
25. Trait selection at character creation
26. Reputation system announcements
27. Companion wheel accessibility
28. Weapon condition announcements
29. Ammo type selection
30. Hardcore mode status (if enabled)

**Phase 5 — Nice to Have:**
31. Caravan card game
32. Casino minigames
33. Challenge tracking
34. Implant menu

---

## Key References

- [xNVSE GitHub](https://github.com/xNVSE/NVSE) — Script extender source and plugin API
- [xNVSE DEVELOPMENT.md](https://github.com/xNVSE/NVSE/blob/master/DEVELOPMENT.md) — Plugin development guide
- [JIP LN NVSE](https://github.com/jazzisparis/JIP-LN-NVSE) — Major NVSE extension (1000+ functions, menu event handlers)
- [yUI-NVSE](https://github.com/yvileapsis/yUI-NVSE) — UI-focused NVSE plugin (reference for tile manipulation)
- [GECK Wiki: Understanding XML](https://geckwiki.com/index.php?title=Tutorial:_Understanding_XML) — Gamebryo tile system documentation
- [GECK Wiki: NVSE Event Handling](https://geckwiki.com/index.php?title=NVSE_Event_Handling) — Event system documentation
- [GECK Wiki: GetUIFloat](https://geckwiki.com/index.php/GetUIFloat) — UI tile value access
- [UESP: Oblivion XML](https://en.uesp.net/wiki/Oblivion_Mod:Oblivion_XML) — Detailed tile system documentation (same system as FNV)
- [Tolk](https://github.com/dkager/tolk) — Proven 32-bit screen reader abstraction library
- [SRAL](https://github.com/m1maker/SRAL) — Screen reader abstraction library used by fallout4access
- [Scaleform GFx - Wikipedia](https://en.wikipedia.org/wiki/Scaleform_GFx) — Scaleform middleware documentation

---

## Appendix A: AutoWalk Implementation Deep-Dive

The AutoWalk feature is one of the more complex systems to port because it uses a **hybrid C++/Papyrus architecture** that delegates movement to Fallout 4's built-in AI pathfinding rather than injecting raw input.

### F4A Architecture

```
ObjectScanner (C++)          AutoWalk (C++)           Papyrus Script (F4A:AutoWalk)
─────────────────           ──────────────           ─────────────────────────────
User presses Shift+Home
        │
        ▼
WalkToCurrentObject()
  Validates target
  Speaks "Walking to X"
        │
        ▼
              WalkTo(target, 100.0f, callback)
                        │
                        ▼
              Validate (deleted? disabled? null?)
              Distance check (already close?)
                        │
                        ▼
              vm->DispatchMethodCall("OnWalkToTarget")
                        │                     │
                        │                     ▼
                        │            Game.SetPlayerAIDriven(true)
                        │            WalkScene.Start()
                        │            StartTimer(0.25s)
                        │                     │
                   C++ Update()          Timer fires
                   on input events       every 0.25s
                        │                     │
                        ▼                     ▼
                   CheckArrival()        CheckArrival()
                   (tolerance +30)       (tolerance +20)
                        │                     │
                        ▼                     ▼
                   Call callback         StopWalking()
                   Speak "Arrived"       SetLookAt(target)
                   Rotate player         SetPlayerAIDriven(false)
```

### Key Design Decisions in F4A

1. **AI-Driven Movement** — Uses `Game.SetPlayerAIDriven(true)` + a Papyrus Scene to move the player. The game's own navmesh pathfinding handles obstacles. No raw input injection or manual angle calculation for direction.

2. **Papyrus Quest Dependency** — Requires `fallout4access.esp` with a quest form (`F4A_AutoWalkQuest`). The C++ code dispatches to Papyrus via `RE::GameVM::DispatchMethodCall()`.

3. **FormID as int32** — FormIDs are passed as `int32_t` to Papyrus (not float) to prevent precision loss with DLC/mod object IDs.

4. **Dual Arrival Detection** — Both C++ (on every input event) and Papyrus (timer every 0.25s) independently check distance to target.

5. **Cancellation Sources:**
   - Manual: WASD/Escape/Space key press
   - Combat: `OnCombatStateChanged` event auto-stops walk
   - Invalid target: deleted/disabled reference detected during arrival check

### F4A Source Files

| File | Role |
|------|------|
| `src/AutoWalk.h` | Public API: `Initialize()`, `WalkTo()`, `Stop()`, `IsWalking()`, `Update()` |
| `src/AutoWalk.cpp` | C++ side: validation, Papyrus VM dispatch, arrival checking |
| `src/ObjectScanner.cpp` | Integration: Shift+Home trigger, Update() calls, WASD cancellation |
| `F4A/AutoWalk.psc` | Papyrus side: `SetPlayerAIDriven()`, scene start, timer-based arrival, combat detection |

### F4A Game API Dependencies

| API | Purpose |
|-----|---------|
| `RE::PlayerCharacter::GetSingleton()` | Player reference |
| `RE::TESObjectREFR::GetPosition()` / `GetDistance()` | Spatial queries |
| `RE::GameVM::DispatchMethodCall()` | C++ to Papyrus bridge |
| `RE::TESForm::GetFormByEditorID()` | Quest form lookup |
| `RE::TESObjectREFR::IsDeleted()` / `IsDisabled()` | Target validation |
| `Game.SetPlayerAIDriven(true/false)` | Papyrus: enable/disable AI movement |
| `Scene.Start()` / `Scene.Stop()` | Papyrus: scene-driven walking |
| `PlayerRef.SetLookAt()` | Papyrus: face target on arrival |
| `GetAsyncKeyState()` | Win32: detect cancel keys |

### FNV Porting Analysis

#### What Transfers

- **Overall architecture** — The hybrid C++/Papyrus approach is valid for FNV. The GECK scripting language (FNV's equivalent of Papyrus) supports `SetPlayerAIDriven`, `GetDistance`, timer events, and package-driven movement.
- **Arrival detection logic** — Distance-based threshold checking is engine-agnostic.
- **Cancellation design** — Combat detection, key polling, target validation all conceptually transfer.
- **ObjectScanner integration pattern** — The trigger/callback pattern is reusable.

#### What Changes

| Aspect | F4A (Fallout 4) | FNV Equivalent | Effort |
|--------|-----------------|----------------|--------|
| **C++ to Script bridge** | `RE::GameVM::DispatchMethodCall()` | NVSE `CallFunction()` or custom event dispatch | Major rewrite |
| **ESP quest form** | Created in FO4 Creation Kit | Created in GECK | New asset needed |
| **Scene-based movement** | Papyrus Scene + AI packages | GECK scene or AI package + `SetPlayerAIDriven` | Redesign — FNV scenes are less flexible than FO4 Papyrus scenes |
| **Script language** | Papyrus (`.psc`, compiled `.pex`) | GECK script (`.gek` text scripts, compiled into ESP) | Full rewrite of script |
| **FormID passing** | `DispatchMethodCall` with int32 param | NVSE `CallFunction` or shared global variable | Different mechanism |
| **Player rotation on arrival** | `player->SetAngleOnReference()` | `SetAngle` console command or `SetPos`/`SetAngle` script functions | Minor adaptation |
| **Combat event** | `OnCombatStateChanged` Papyrus event | `OnCombatEnd` / NVSE `SetEventHandler "OnCombatStateChanged"` | Minor adaptation |

#### FNV-Specific Concerns

1. **`SetPlayerAIDriven` exists in FNV** — This is the same Gamebryo function. It works in GECK scripts. This is the core movement mechanism and it ports.

2. **GECK Scenes vs Papyrus Scenes** — FNV scenes are authored in the GECK and are less dynamic than FO4 Papyrus scenes. An alternative approach for FNV:
   - Use an AI package with a `Travel` procedure targeting the destination
   - Or use `SetPlayerAIDriven(1)` + `player.MoveTo` with forced AI travel
   - The JIP LN NVSE plugin may provide additional movement functions

3. **C++ to GECK Script Communication** — FNV has no `DispatchMethodCall`. Options:
   - Set a global variable from C++ that the GECK script polls on a timer
   - Use NVSE's `CallFunction` to invoke a script function directly
   - Use a custom NVSE event that the GECK script listens for
   - Write the entire AutoWalk in C++ without a script dependency (simpler but loses navmesh pathfinding)

4. **Pure C++ Alternative** — Since FNV's GECK scripting is less capable than Papyrus, it may be simpler to implement AutoWalk entirely in C++:
   - Use `kMessage_MainGameLoop` for per-frame updates
   - Set player AI driven via direct engine function call
   - Create and assign AI packages from C++ (JIP LN NVSE has functions for this)
   - Handle arrival and cancellation entirely in C++
   - This eliminates the ESP dependency but requires more reverse engineering

5. **No `GetAsyncKeyState` concern** — This Win32 API works the same in 32-bit FNV.

#### Recommended FNV Approach

**Option A: Hybrid C++/GECK Script (closest to F4A)**
- Create `fnvaccess.esp` in GECK with a quest + script
- C++ sets global variables or calls script functions via NVSE
- Script handles `SetPlayerAIDriven`, movement packages, timers
- Pro: Uses game pathfinding. Con: Requires ESP, GECK scripting is clunkier.

**Option B: Pure C++ (simpler, no ESP)**
- All logic in the NVSE plugin DLL
- Call `SetPlayerAIDriven` directly via reverse-engineered function address
- Manage movement via direct function calls (JIP LN NVSE reference)
- Use `kMessage_MainGameLoop` for update loop
- Pro: No ESP dependency, single codebase. Con: More reverse engineering, may lack smooth pathfinding.

**Recommendation:** Start with **Option B** (pure C++) for the initial port since it avoids the GECK toolchain complexity. Fall back to **Option A** if navmesh pathfinding quality is insufficient with direct movement.
