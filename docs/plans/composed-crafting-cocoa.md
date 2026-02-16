# LOS Audio Feedback via Papyrus VM - Implementation Plan

## Background

The direct C++ BSAudioManager APIs (`GetSoundHandleByName`, `GetSoundHandleByFile`) crash when playing custom mod sounds. Both crash in `BSSoundHandle::Play()` because the engine's internal audio hash tables don't have entries for the sound (R14=1 sentinel). The working AudioBeaconsForBlind mod plays sounds via Papyrus `Sound.Play()`, which uses a different internal code path that properly loads loose WAV files.

## Approach

Replace the broken `BSAudioManager` / `BSSoundHandle` approach with Papyrus VM `DispatchMethodCall` to call `Sound.Play()` and `Sound.StopInstance()` from C++.

Papyrus `Sound` script methods on `BGSSoundDescriptorForm`:
- `int Play(ObjectReference akSource)` — starts playback, returns instance ID
- `StopInstance(int aiSoundInstanceID)` — stops a playing instance

## Files to Modify

### 1. `src/AutoAimAccessibility.h`

**Replace includes** — remove `BSAudioManager.h`, `BSSoundHandle.h`, `BSResource_ID.h`; add `BGSSoundDescriptorForm.h`.

**Replace audio members:**
```cpp
// Remove:
RE::BSSoundHandle m_losSound;

// Add:
RE::BGSSoundDescriptorForm* m_losSoundForm = nullptr;  // cached SNDR form
std::uint32_t m_losSoundInstanceID = 0;                 // Papyrus sound instance ID
```

### 2. `src/AutoAimAccessibility.cpp`

**Replace `StartLOSSound()`:**
```cpp
void AutoAimAccessibility::StartLOSSound()
{
    if (m_losSoundPlaying) return;

    // Cache SNDR form lookup
    if (!m_losSoundForm) {
        m_losSoundForm = RE::TESDataHandler::GetSingleton()->LookupForm<RE::BGSSoundDescriptorForm>(
            0x006C57, "fallout4access.esp");
        if (!m_losSoundForm) {
            REX::ERROR("AutoAimAccessibility: SNDR form not found");
            return;
        }
    }

    auto* gameVM = RE::GameVM::GetSingleton();
    if (!gameVM) return;
    auto vm = gameVM->GetVM();
    if (!vm) return;

    auto& handlePolicy = vm->GetObjectHandlePolicy();
    auto handle = handlePolicy.GetHandleForObject(
        static_cast<std::uint32_t>(RE::ENUM_FORM_ID::kSNDR),
        m_losSoundForm);

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return;

    // Call Sound.Play(player) via Papyrus VM
    // Use a callback to capture the returned sound instance ID
    auto callback = RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor>(
        new LOSSoundCallback(this));

    vm->DispatchMethodCall(
        handle,
        RE::BSFixedString("Sound"),
        RE::BSFixedString("Play"),
        callback,
        player);

    m_losSoundPlaying = true;
}
```

**Replace `StopLOSSound()`:**
```cpp
void AutoAimAccessibility::StopLOSSound()
{
    if (!m_losSoundPlaying) return;

    if (m_losSoundInstanceID != 0 && m_losSoundForm) {
        auto* gameVM = RE::GameVM::GetSingleton();
        if (gameVM) {
            auto vm = gameVM->GetVM();
            if (vm) {
                auto& handlePolicy = vm->GetObjectHandlePolicy();
                auto handle = handlePolicy.GetHandleForObject(
                    static_cast<std::uint32_t>(RE::ENUM_FORM_ID::kSNDR),
                    m_losSoundForm);

                vm->DispatchMethodCall(
                    handle,
                    RE::BSFixedString("Sound"),
                    RE::BSFixedString("StopInstance"),
                    nullptr,
                    static_cast<std::int32_t>(m_losSoundInstanceID));
            }
        }
    }

    m_losSoundPlaying = false;
    m_losSoundInstanceID = 0;
}
```

**Add callback class** (in .cpp file, before StartLOSSound):
```cpp
class LOSSoundCallback : public RE::BSScript::IStackCallbackFunctor
{
public:
    LOSSoundCallback(AutoAimAccessibility* a_owner) : m_owner(a_owner) {}

    void CallQueued() override {}
    void CallCanceled() override {}
    void StartMultiDispatch() override {}
    void EndMultiDispatch() override {}
    void operator()(RE::BSScript::Variable a_result) override
    {
        if (a_result.is<std::int32_t>()) {
            auto id = RE::BSScript::get<std::int32_t>(a_result);
            if (id) {
                m_owner->SetLOSSoundInstanceID(static_cast<std::uint32_t>(*id));
            }
        }
    }
    bool CanSave() override { return false; }

private:
    AutoAimAccessibility* m_owner;
};
```

**Add setter in header** (public section):
```cpp
void SetLOSSoundInstanceID(std::uint32_t a_id) { m_losSoundInstanceID = a_id; }
```

**New includes in .cpp:**
```cpp
#include <RE/T/TESDataHandler.h>
#include <RE/B/BGSSoundDescriptorForm.h>
#include <RE/G/GameScript.h>
#include <RE/B/BSScript_IVirtualMachine.h>
#include <RE/B/BSScriptUtil.h>
#include <RE/B/BSScript_IStackCallbackFunctor.h>
#include <RE/E/ENUM_FORM_ID.h>
```

## No Other Changes Needed

The rest of the auto-aim system (LOS priority scoring, VATS check, tracking loop, OnEnterIronsights, StopTracking, UpdateTracking) is already working correctly. Only `StartLOSSound()` and `StopLOSSound()` need to change.

## Verification

1. Build: `xmake build`
2. Install: `xmake install-mo2` (or `/deploy`)
3. Test in-game:
   - Aim at visible enemy → LOS sound plays
   - Enemy goes behind wall → sound stops
   - Enemy becomes visible → sound resumes
   - Exit ironsights → sound stops immediately
   - No crash on any of these transitions
