#include "AutoWalk.h"
#include "PapyrusDispatch.h"
#include "SpeechManager.h"

#include <RE/B/BSScript_IVirtualMachine.h>
#include <RE/B/BSScript_Variable.h>

// Windows winioctl.h declares a `DETECTION_TYPE` typedef that collides with
// RE::DETECTION_TYPE pulled in via `using namespace RE;` in pch.h. Mask the
// Windows name during preprocessing so the typedef lands under a unique name.
#define DETECTION_TYPE WIN32_DETECTION_TYPE
#include <Windows.h>
#undef DETECTION_TYPE
#undef ERROR

// Control-map events whose keyboard mappings cancel an in-progress autowalk.
// Indices must match the size of AutoWalk::m_cancelKeyLatched.
namespace
{
	struct CancelEvent
	{
		const char*                      name;
		RE::UserEvents::INPUT_CONTEXT_ID context;
	};

	constexpr CancelEvent kCancelEvents[] = {
		{ "Forward", RE::UserEvents::INPUT_CONTEXT_ID::kMainGameplay },
		{ "Back", RE::UserEvents::INPUT_CONTEXT_ID::kMainGameplay },
		{ "StrafeLeft", RE::UserEvents::INPUT_CONTEXT_ID::kMainGameplay },
		{ "StrafeRight", RE::UserEvents::INPUT_CONTEXT_ID::kMainGameplay },
		{ "Sprint", RE::UserEvents::INPUT_CONTEXT_ID::kMainGameplay },
		{ "Jump", RE::UserEvents::INPUT_CONTEXT_ID::kMainGameplay },
		{ "Pause", RE::UserEvents::INPUT_CONTEXT_ID::kMainGameplay },
	};
	static_assert(std::size(kCancelEvents) == AutoWalk::CANCEL_KEY_COUNT,
		"kCancelEvents must match AutoWalk::CANCEL_KEY_COUNT");

	bool IsKeyPressedNow(std::uint32_t a_vk)
	{
		return a_vk != RE::ControlMap::kInvalid && (GetAsyncKeyState(static_cast<int>(a_vk)) & 0x8000) != 0;
	}

	// Do-nothing Papyrus stack callback. A non-null callback is REQUIRED when
	// dispatching a latent function (F4A:AutoWalkHelper.WalkPlayerTo calls the
	// latent Actor.PathToReference): the VM's latent-return path dereferences
	// the stack's IStackCallbackFunctor without a null check. Non-latent calls
	// (e.g. F4A:SoundHelper.PlaySound) take the inline-completion path, which
	// null-guards the callback, so they can pass nullptr. The object is
	// ref-counted; a fresh instance is handed to BSTSmartPointer, the VM keeps
	// a reference until the stack completes, then it is freed.
	class NoOpStackCallback : public RE::BSScript::IStackCallbackFunctor
	{
	public:
		void CallQueued() override {}
		void CallCanceled() override {}
		void StartMultiDispatch() override {}
		void EndMultiDispatch() override {}
		void operator()(RE::BSScript::Variable) override {}
		bool CanSave() override { return false; }
	};

	RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> MakeNoOpCallback()
	{
		return RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor>{ new NoOpStackCallback() };
	}
}

// Note: the out-of-line definition of the CommonLibF4-declared
// IStackCallbackFunctor destructor (needed so the subclass above links) is
// provided by AutoAimAccessibility.cpp; do not duplicate it here.

static AutoWalk* s_singleton = nullptr;

AutoWalk* AutoWalk::GetSingleton()
{
	if (!s_singleton) {
		s_singleton = new AutoWalk();
	}
	return s_singleton;
}

void AutoWalk::Initialize()
{
	if (m_initialized)
		return;

	// No ESP/quest lookup: auto-walk drives the player through the loose global
	// script F4A:AutoWalkHelper, which the VM lazy-loads by name on first
	// dispatch (same mechanism as F4A:SoundHelper). The feature is therefore
	// available in any load order, including ones where our quest never binds.
	m_initialized = true;
	REX::INFO("AutoWalk: Initialized (helper-script mode)");
}

void AutoWalk::WalkTo(RE::TESObjectREFR* a_target, float a_stopDistance,
	std::function<void()> a_onArrival)
{
	if (!m_initialized) {
		REX::WARN("AutoWalk: Not initialized");
		return;
	}

	if (!a_target || a_target->IsDeleted() || a_target->IsDisabled()) {
		REX::WARN("AutoWalk: Invalid target");
		return;
	}

	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player)
		return;

	RE::NiPoint3 targetPos = a_target->GetPosition();
	RE::NiPoint3 playerPos = player->GetPosition();
	float        distance = playerPos.GetDistance(targetPos);

	if (distance <= a_stopDistance) {
		// Already close enough
		REX::INFO("AutoWalk: Already within {} units", a_stopDistance);
		if (a_onArrival)
			a_onArrival();
		return;
	}

	// Store state for arrival callback
	m_targetRef = a_target;
	m_stopDistance = a_stopDistance;
	m_onArrival = a_onArrival;

	// Seed the cancel-key latches. If the user's autowalk hotkey shares a
	// key with any cancel action (the common case is Shift: autowalk bound
	// to Shift+E, Sprint bound to Shift), that key will still be held in
	// the first Update() tick. Mark each currently-held cancel key as
	// latched so it won't trigger a cancel until the user releases it.
	auto* controlMap = RE::ControlMap::GetSingleton();
	for (std::size_t i = 0; i < CANCEL_KEY_COUNT; ++i) {
		m_cancelKeyLatched[i] = false;
		if (!controlMap) {
			continue;
		}
		auto vk = controlMap->GetMappedKey(kCancelEvents[i].name,
			RE::INPUT_DEVICE::kKeyboard,
			kCancelEvents[i].context);
		if (IsKeyPressedNow(vk)) {
			m_cancelKeyLatched[i] = true;
		}
	}

	// Drive the player through the loose global script. WalkPlayerTo is LATENT
	// (Actor.PathToReference), so a non-null callback is mandatory — see
	// NoOpStackCallback. The FormID is passed as int32 (no float precision loss
	// and no ref-packing crash); the script resolves it with Game.GetForm.
	auto* gameVM = RE::GameVM::GetSingleton();
	if (!gameVM)
		return;
	auto vm = gameVM->GetVM();
	if (!vm)
		return;

	std::int32_t formID = static_cast<std::int32_t>(a_target->GetFormID());
	bool         dispatched = PapyrusDispatch::DispatchStatic(vm.get(),
		RE::BSFixedString("F4A:AutoWalkHelper"),
		RE::BSFixedString("WalkPlayerTo"),
		MakeNoOpCallback(),
		&formID);

	if (!dispatched) {
		REX::ERROR("AutoWalk: DispatchStaticCall WalkPlayerTo failed");
		SpeechManager::GetSingleton()->Speak("Auto-walk unavailable");
		m_targetRef = nullptr;
		m_onArrival = nullptr;
		return;
	}

	const char*  name = a_target->GetDisplayFullName();
	RE::NiPoint3 tpos = a_target->GetPosition();
	REX::INFO("AutoWalk: Dispatched walk to FormID {:08X} for {}, distance: {}",
		a_target->GetFormID(), name ? name : "<unnamed>", distance);
	REX::INFO("AutoWalk: Player pos: ({:.0f}, {:.0f}, {:.0f}), Target pos: ({:.0f}, {:.0f}, {:.0f})",
		playerPos.x, playerPos.y, playerPos.z, tpos.x, tpos.y, tpos.z);
}

void AutoWalk::DispatchStopWalk()
{
	auto* gameVM = RE::GameVM::GetSingleton();
	if (!gameVM)
		return;
	auto vm = gameVM->GetVM();
	if (!vm)
		return;

	// Pass the no-op callback for uniformity; nullptr arg = no Papyrus args.
	PapyrusDispatch::DispatchStatic(vm.get(),
		RE::BSFixedString("F4A:AutoWalkHelper"),
		RE::BSFixedString("StopWalk"),
		MakeNoOpCallback(),
		nullptr);
}

void AutoWalk::Stop()
{
	if (!m_targetRef)
		return;

	DispatchStopWalk();

	m_targetRef = nullptr;
	m_onArrival = nullptr;

	REX::INFO("AutoWalk: Stopped");
}

void AutoWalk::Update()
{
	// Skip if no target
	if (!m_targetRef)
		return;

	// Check for cancel keys using ControlMap to respect remapped controls.
	// Uses GetAsyncKeyState polling because AI-driven movement blocks normal
	// input events. Edge-triggered: any cancel key that was already held at
	// WalkTo() time (the m_cancelKeyLatched flag) must be released before it
	// can trigger a cancel, otherwise autowalk would self-cancel instantly
	// whenever the hotkey shares a key with a cancel action.
	auto* controlMap = RE::ControlMap::GetSingleton();
	if (controlMap) {
		for (std::size_t i = 0; i < CANCEL_KEY_COUNT; ++i) {
			auto vk = controlMap->GetMappedKey(kCancelEvents[i].name,
				RE::INPUT_DEVICE::kKeyboard,
				kCancelEvents[i].context);
			bool pressed = IsKeyPressedNow(vk);

			if (m_cancelKeyLatched[i]) {
				// Still latched from the start of the walk. Clear the
				// latch once the key is released; skip this key until then.
				if (!pressed) {
					m_cancelKeyLatched[i] = false;
				}
				continue;
			}

			if (pressed) {
				REX::INFO("AutoWalk: Cancel key '{}' detected, stopping", kCancelEvents[i].name);
				SpeechManager::GetSingleton()->Speak("Stopped");
				Stop();
				return;
			}
		}
	}

	// Check if target is still valid
	if (m_targetRef->IsDeleted() || m_targetRef->IsDisabled()) {
		REX::WARN("AutoWalk: Target became invalid");
		Stop();
		return;
	}

	// Check arrival (Papyrus also checks, but we need to call our callback)
	CheckArrival();
}

void AutoWalk::CheckArrival()
{
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player || !m_targetRef)
		return;

	RE::NiPoint3 playerPos = player->GetPosition();
	RE::NiPoint3 targetPos = m_targetRef->GetPosition();
	float        distance = playerPos.GetDistance(targetPos);

	if (distance <= m_stopDistance + ARRIVAL_TOLERANCE) {
		REX::INFO("AutoWalk: Arrived (distance: {})", distance);

		// The helper's PathToReference is latent and would keep walking the
		// player into the object; halt it here so we stop at the configured
		// distance and hand control back to the player.
		DispatchStopWalk();

		auto callback = m_onArrival;  // Copy before clearing state

		m_targetRef = nullptr;
		m_onArrival = nullptr;

		if (callback) {
			callback();
		}
	}
}
