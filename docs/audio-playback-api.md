# Audio Playback API Reference

How to play sounds and audio cues in F4SE plugins using CommonLibF4.

## Quick Start (Recommended)

The simplest way to play UI sounds:

```cpp
#include <RE/U/UIUtils.h>

// Play menu sounds
RE::UIUtils::PlayMenuSound("UIMenuOK");       // Confirmation
RE::UIUtils::PlayMenuSound("UIMenuCancel");   // Cancel/error
RE::UIUtils::PlayMenuSound("UIMenuChange");   // Selection change

// Pip-Boy variant (slightly different sound)
RE::UIUtils::PlayPipboySound("UIMenuOK");
```

## Built-in Sound Names

| Sound Name | Use Case |
|------------|----------|
| `UIMenuOK` | Selection confirm, target acquired |
| `UIMenuCancel` | Back, cancel, target lost |
| `UIMenuOpen` | Menu/state opened |
| `UIMenuClose` | Menu/state closed |
| `UIMenuChange` | List selection changed |
| `UIMenuChange_Alt` | Alternative selection sound |
| `UICharacterGenWarning` | Warning notification |
| `UIMessage` | Generic notification |
| `UITerminalTyping` | Terminal input |
| `UITerminalActivate` | Terminal access |
| `UITerminalError` | Terminal error |
| `UIPickupItem` | Item pickup |
| `UIDropItem` | Item drop |

## Custom Mod Sounds via Papyrus VM (Recommended)

For playing custom sounds from an ESP plugin (e.g. loose WAV files referenced by SNDR forms), use the Papyrus VM helper script. The C++ `BSAudioManager` APIs (`GetSoundHandleByName`, `GetSoundHandleByFile`) **crash** on custom mod sounds because `BSSoundHandle::Play()` fails when the engine's internal audio hash tables lack entries for the sound.

### Why Not BSAudioManager?

Both `GetSoundHandleByName` and `GetSoundHandleByFile` crash in `BSSoundHandle::Play()` for mod-added sounds. The engine uses internal hash tables that don't have entries for custom SNDR forms. The Papyrus `Sound.Play()` native function uses a different code path that properly loads loose WAV files.

### Why Not DispatchMethodCall?

Calling `Sound.Play()` directly via `IVirtualMachine::DispatchMethodCall` on a SNDR form handle also fails — `DispatchMethodCall` returns `false` because SNDR forms don't have bound Papyrus script objects. Additionally, passing a `BGSSoundDescriptorForm*` as an argument to any dispatch call crashes in `PackVariable` when it tries to create/bind a BSScript::Object for the form.

### Why Not Pass TESObjectREFR* as Arguments?

Passing `TESObjectREFR*` or `Actor*` (e.g., the player) to `DispatchStaticCall` as a Papyrus `ObjectReference` parameter causes `"Type mismatch for argument"` errors in the Papyrus VM. The `PackVariable` template creates a BSScript::Object that Papyrus rejects. **Always have the Papyrus script obtain references internally** (e.g., `Game.GetPlayer()`) instead of passing them from C++.

### Solution: Papyrus Helper Script + DispatchStaticCall

**Step 1:** Create a SNDR form in your ESP pointing to the WAV file.

**Step 2:** Use the `F4A:SoundHelper` Papyrus script (`Data/Scripts/Source/User/F4A/SoundHelper.psc`):

```papyrus
Scriptname F4A:SoundHelper

int Function PlaySound(int aiSoundFormID) global
    Sound snd = Game.GetForm(aiSoundFormID) as Sound
    if snd
        return snd.Play(Game.GetPlayer())
    endIf
    return 0
EndFunction

Function StopSound(int aiInstanceID) global
    if aiInstanceID > 0
        Sound.StopInstance(aiInstanceID)
    endIf
EndFunction
```

**Step 3:** Call from C++ via `DispatchStaticCall`:

```cpp
#include <RE/T/TESDataHandler.h>
#include <RE/G/GameScript.h>
#include <RE/B/BSScript_IVirtualMachine.h>
#include <RE/B/BSScriptUtil.h>
#include <RE/B/BSScript_IStackCallbackFunctor.h>
#include <RE/B/BSScript_Variable.h>

// Look up SNDR form (cache this)
auto* soundForm = RE::TESDataHandler::GetSingleton()
    ->LookupForm<RE::BGSSoundDescriptorForm>(0x006C57, "fallout4access.esp");

// Get VM
auto* gameVM = RE::GameVM::GetSingleton();
auto vm = gameVM->GetVM();
auto* player = RE::PlayerCharacter::GetSingleton();

// Play — pass form ID as int, NOT the form pointer
// Do NOT pass TESObjectREFR*/Actor* — causes Papyrus type mismatch errors
auto callback = RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor>(
    new MyCallback());

vm->DispatchStaticCall(
    RE::BSFixedString("F4A:SoundHelper"),
    RE::BSFixedString("PlaySound"),
    callback,
    static_cast<std::int32_t>(soundForm->GetFormID()));  // int, not pointer!

// Stop — pass the instance ID returned by PlaySound callback
vm->DispatchStaticCall(
    RE::BSFixedString("F4A:SoundHelper"),
    RE::BSFixedString("StopSound"),
    nullptr,
    static_cast<std::int32_t>(instanceID));
```

### Callback to Capture Instance ID

`Sound.Play()` returns an `int` instance ID needed for `StopInstance`. Subclass `IStackCallbackFunctor` to capture it:

```cpp
// Linker fix: the base dtor is declared in CommonLibF4 but only defined in
// the game binary. Provide a trivial definition so derived classes can link.
RE::BSScript::IStackCallbackFunctor::~IStackCallbackFunctor() = default;

class MySoundCallback : public RE::BSScript::IStackCallbackFunctor
{
public:
    void CallQueued() override {}
    void CallCanceled() override {}
    void StartMultiDispatch() override {}
    void EndMultiDispatch() override {}
    void operator()(RE::BSScript::Variable a_result) override
    {
        if (a_result.is<std::int32_t>()) {
            auto id = RE::BSScript::get<std::int32_t>(a_result);
            if (id > 0) {
                // Store id for later StopSound call
            }
        }
    }
    bool CanSave() override { return false; }
};
```

### Key Rules

- **Always pass SNDR form ID as `int32_t`**, never as a `BGSSoundDescriptorForm*` — `PackVariable` crashes trying to bind a script object to SNDR forms
- **Always use `DispatchStaticCall`** with the helper script, never `DispatchMethodCall` on a SNDR handle
- **The ESP with the SNDR form is required** — it defines which WAV to play, volume, attenuation, etc.
- **The `.pex` script must be installed** to `Data/Scripts/F4A/SoundHelper.pex`
- The callback executes asynchronously on the Papyrus VM thread

## BSAudioManager (Built-in Sounds Only)

For **built-in game sounds** (not custom mod sounds), `BSAudioManager` works fine:

```cpp
#include <RE/B/BSAudioManager.h>
#include <RE/B/BSSoundHandle.h>

auto* audioMgr = RE::BSAudioManager::GetSingleton();
RE::BSSoundHandle handle;
audioMgr->GetSoundHandleByName(handle, "UIMenuOK", 0.0f, 0, nullptr);
handle.Play();
handle.Stop();
handle.FadeOutAndRelease(500);
```

**Do NOT use this for custom mod sounds** — `BSSoundHandle::Play()` will crash.

## Example: Accessibility Audio Helper

```cpp
namespace AudioCue {
    inline void TargetAcquired() {
        RE::UIUtils::PlayMenuSound("UIMenuOK");
    }

    inline void TargetLost() {
        RE::UIUtils::PlayMenuSound("UIMenuCancel");
    }

    inline void SelectionChanged() {
        RE::UIUtils::PlayMenuSound("UIMenuChange");
    }

    inline void Warning() {
        RE::UIUtils::PlayMenuSound("UICharacterGenWarning");
    }
}
```

## Notes

- Sound names are **case-sensitive**
- `PlayMenuSound` / `BSAudioManager`: only for built-in game sounds
- Custom mod sounds: must use Papyrus VM via `F4A:SoundHelper`
- Volume is controlled by the game's audio settings (BGSSoundCategory)
- No direct volume control per-sound; use game's category sliders

## Header Files

| Header | Purpose |
|--------|---------|
| `RE/U/UIUtils.h` | High-level PlayMenuSound/PlayPipboySound |
| `RE/B/BSAudioManager.h` | Audio manager singleton (built-in sounds only) |
| `RE/B/BSSoundHandle.h` | Sound handle for playback control |
| `RE/B/BGSSoundDescriptorForm.h` | Sound descriptor forms (for LookupForm) |
| `RE/G/GameScript.h` | GameVM singleton |
| `RE/B/BSScriptUtil.h` | DispatchStaticCall/DispatchMethodCall templates |
| `RE/B/BSScript_IStackCallbackFunctor.h` | Callback base class |
| `RE/B/BSScript_Variable.h` | Variable type + get\<T\>() |
