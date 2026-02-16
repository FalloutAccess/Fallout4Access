# CommonLib Plugin Libraries Research

Research into libraries commonly used by F4SE and SKSE plugins that use CommonLib.

## Commonly Used Libraries

### Logging & Formatting

| Library | Description | URL |
|---------|-------------|-----|
| **spdlog** | Fast, header-only logging library | https://github.com/gabime/spdlog |
| **fmt** | Python-style string formatting | https://github.com/fmtlib/fmt |

### Configuration Files

| Library | Description | URL |
|---------|-------------|-----|
| **SimpleINI** | Cross-platform INI file reader/writer, header-only | https://github.com/brofield/simpleini |
| **toml++** | TOML config file parser | https://github.com/mareks/tomlplusplus |
| **nlohmann/json** | JSON for Modern C++ | https://github.com/nlohmann/json |

### Assembly & Hooking

| Library | Description | URL |
|---------|-------------|-----|
| **xbyak** | JIT assembler for x86/x64 | https://github.com/herumi/xbyak |
| **Zydis** | x86/x64 disassembler (with Zycore dependency) | https://github.com/zyantific/zydis |

### Utility Libraries

| Library | Description | URL |
|---------|-------------|-----|
| **CLibUtil** | Header-only utilities (string, RNG, INI wrappers, config scanning) | https://github.com/powerof3/CLibUtil |
| **frozen** | Compile-time constexpr containers (maps, sets) | https://github.com/serge-sans-paille/frozen |
| **magic-enum** | Static reflection for enums | https://github.com/Neargye/magic_enum |
| **srell** | ECMAScript-compatible regex library | https://github.com/k-takata/Srell |
| **rsm-binary-io** | Binary I/O utilities | https://github.com/Ryan-rsm-McKenzie/binary_io |

### GUI/Overlay

| Library | Description | URL |
|---------|-------------|-----|
| **Dear ImGui** | Immediate mode GUI for overlays | https://github.com/ocornut/imgui |

### Data Structures

| Library | Description | URL |
|---------|-------------|-----|
| **rapidcsv** | Header-only CSV parser | https://github.com/d99kris/rapidcsv |
| **unordered_dense** | Fast hash map implementation | https://github.com/martinus/unordered_dense |
| **robin-hood-hashing** | Fast hash map (alternative) | https://github.com/martinus/robin-hood-hashing |

### Other

| Library | Description | URL |
|---------|-------------|-----|
| **boost-stl-interfaces** | Boost STL interfaces | Part of Boost |
| **catch2** | Unit testing framework | https://github.com/catchorg/Catch2 |
| **tbb** | Intel Threading Building Blocks | https://github.com/oneapi-src/oneTBB |
| **infoware** | Hardware/system information library | https://github.com/ThePhD/infoware |

---

## Recommendations for Fallout4Access

### High Priority

1. **SimpleINI** - Store user preferences
   - Speech verbosity level
   - Navigation key rebinding
   - Which menus to announce
   - Speech interrupt behavior
   - Enable/disable specific accessibility features

2. **xbyak** - Custom ASM trampolines and mid-function hooks
   - Intercept execution at arbitrary code points (not just CALL/JMP)
   - Access/modify CPU register values at hook point
   - Conditional hooks based on runtime values
   - See [xbyak-usage-guide.md](./xbyak-usage-guide.md) for detailed patterns

3. **CLibUtil** - Utility functions
   - String utilities (splitting, joining, case-insensitive compare)
   - SimpleINI wrappers for easier config access
   - Compile-time FNV-1A string hashing

4. **frozen** - Compile-time lookup tables
   - Menu path to accessible name mappings
   - Key code to action name mappings
   - Form ID to spoken description mappings

5. **magic-enum** - Enum to string conversion
   - Equipment states
   - Menu modes
   - Quest statuses

### Medium Priority

6. **fmt** - String formatting
   - Cleaner string formatting for speech output
   - Already bundled with spdlog/CommonLibF4

### Low Priority (Only if needed)

7. **nlohmann/json** - Complex structured data
8. **rapidcsv** - Loading tabular data files

---

## Example Integration

### xmake.lua additions

```lua
add_requires("simpleini")
add_requires("xbyak")
add_requires("frozen")
add_requires("magic-enum")
```

### vcpkg.json

```json
{
  "dependencies": [
    "simpleini",
    "xbyak",
    "frozen",
    "magic-enum"
  ]
}
```

---

## Reference Projects

Projects analyzed for this research:

| Project | Description | URL |
|---------|-------------|-----|
| Buffout4 | Crash logger and bug fixer | https://github.com/alandtse/Buffout4 |
| PapyrusExtenderSSE | Papyrus function library | https://github.com/powerof3/PapyrusExtenderSSE |
| CommonLibSSE-NG | CommonLib for Skyrim SE/AE/VR | https://github.com/CharmedBaryon/CommonLibSSE-NG |
| dMenu | ImGui cheat menu for Skyrim | https://github.com/D7ry/dMenu |
| F4SE-Plugins | F4SE plugin examples | https://github.com/Ez0n3/F4SE-Plugins |
| commonlibf4-template | F4SE plugin template | https://github.com/libxse/commonlibf4-template |
| MCM | Mod Configuration Menu | https://github.com/reg2k/f4mcm |

---

## Dependencies by Project

### Buffout4
- args, boost-algorithm, boost-container, boost-nowide, boost-stacktrace
- boost-stl-interfaces, catch2, fmt, frozen, infoware (d3d)
- magic-enum, nowide, openvr, robin-hood-hashing, rapidcsv
- rsm-binary-io, rsm-mmio, srell, spdlog, tbb
- tomlplusplus, xbyak, zycore, zydis

### PapyrusExtenderSSE
- clib-util, frozen, rsm-binary-io, srell
- spdlog, unordered-dense, xbyak

### CommonLibSSE-NG
- rapidcsv, spdlog, catch2 (for tests)

### dMenu (ImGui-based)
- fast-cpp-csv-parser, rsm-binary-io, simpleini
- spdlog, xbyak, rapidcsv, nlohmann-json
- imgui (dx11-binding, win32-binding)
