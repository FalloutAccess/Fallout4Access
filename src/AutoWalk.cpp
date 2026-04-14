#include "AutoWalk.h"
#include "SpeechManager.h"
#include <Windows.h>
#undef ERROR

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

	// Find the quest by EditorID
	m_quest = RE::TESForm::GetFormByEditorID<RE::TESQuest>("F4A_AutoWalkQuest");
	if (!m_quest) {
		REX::WARN("AutoWalk: F4A_AutoWalkQuest not found");
		REX::WARN("AutoWalk: Auto-walk feature disabled. Install fallout4access.esp to enable.");
		return;
	}

	m_initialized = true;
	REX::INFO("AutoWalk: Initialized successfully");
}

void AutoWalk::WalkTo(RE::TESObjectREFR* a_target, float a_stopDistance,
	std::function<void()> a_onArrival)
{
	if (!m_initialized) {
		REX::WARN("AutoWalk: Not initialized (ESP not loaded?)");
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
	m_lastDistance = distance;
	m_stuckFrames = 0;

	// Dispatch to Papyrus with FormID as int32 (no float precision loss)
	auto* gameVM = RE::GameVM::GetSingleton();
	if (!gameVM)
		return;
	auto vm = gameVM->GetVM();
	if (!vm)
		return;

	auto& handlePolicy = vm->GetObjectHandlePolicy();
	auto  handle = handlePolicy.GetHandleForObject(
		RE::BSScript::GetVMTypeID<RE::TESQuest>(),
		m_quest);

	if (handle == handlePolicy.EmptyHandle()) {
		REX::ERROR("AutoWalk: Failed to get quest VM handle");
		m_targetRef = nullptr;
		m_onArrival = nullptr;
		return;
	}

	bool dispatched = vm->DispatchMethodCall(
		handle,
		RE::BSFixedString("F4A:AutoWalk"),
		RE::BSFixedString("OnWalkToTarget"),
		nullptr,
		static_cast<std::int32_t>(a_target->GetFormID()),
		a_stopDistance);

	if (!dispatched) {
		REX::ERROR("AutoWalk: DispatchMethodCall OnWalkToTarget failed");
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

void AutoWalk::Stop()
{
	if (!m_targetRef)
		return;

	if (m_initialized && m_quest) {
		auto* gameVM = RE::GameVM::GetSingleton();
		if (gameVM) {
			auto vm = gameVM->GetVM();
			if (vm) {
				auto& handlePolicy = vm->GetObjectHandlePolicy();
				auto  handle = handlePolicy.GetHandleForObject(
					RE::BSScript::GetVMTypeID<RE::TESQuest>(),
					m_quest);

				if (handle != handlePolicy.EmptyHandle()) {
					vm->DispatchMethodCall(
						handle,
						RE::BSFixedString("F4A:AutoWalk"),
						RE::BSFixedString("OnStopWalking"),
						nullptr);
				}
			}
		}
	}

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
	// Uses GetAsyncKeyState polling because AI-driven movement blocks normal input events.
	auto* controlMap = RE::ControlMap::GetSingleton();
	if (controlMap) {
		// Look up the VK codes for movement actions from the game's control map
		const std::pair<const char*, RE::UserEvents::INPUT_CONTEXT_ID> cancelEvents[] = {
			{ "Forward", RE::UserEvents::INPUT_CONTEXT_ID::kMainGameplay },
			{ "Back", RE::UserEvents::INPUT_CONTEXT_ID::kMainGameplay },
			{ "StrafeLeft", RE::UserEvents::INPUT_CONTEXT_ID::kMainGameplay },
			{ "StrafeRight", RE::UserEvents::INPUT_CONTEXT_ID::kMainGameplay },
			{ "Sprint", RE::UserEvents::INPUT_CONTEXT_ID::kMainGameplay },
			{ "Jump", RE::UserEvents::INPUT_CONTEXT_ID::kMainGameplay },
			{ "Pause", RE::UserEvents::INPUT_CONTEXT_ID::kMainGameplay },
		};

		bool cancelPressed = false;
		for (const auto& [eventName, context] : cancelEvents) {
			auto vk = controlMap->GetMappedKey(eventName, RE::INPUT_DEVICE::kKeyboard, context);
			if (vk != RE::ControlMap::kInvalid && (GetAsyncKeyState(vk) & 0x8000)) {
				cancelPressed = true;
				break;
			}
		}

		if (cancelPressed) {
			REX::INFO("AutoWalk: Cancel key detected, stopping");
			SpeechManager::GetSingleton()->Speak("Stopped");
			Stop();
			return;
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

		auto callback = m_onArrival;  // Copy before clearing state

		// Clear our state (Papyrus handles its own cleanup)
		m_targetRef = nullptr;
		m_onArrival = nullptr;

		if (callback) {
			callback();
		}
		return;
	}

	// Stuck detection: check if we're making progress
	if (m_lastDistance - distance >= PROGRESS_THRESHOLD) {
		// Making progress - reset stuck counter
		m_stuckFrames = 0;
		m_lastDistance = distance;
	} else {
		m_stuckFrames++;
		if (m_stuckFrames >= STUCK_FRAME_LIMIT) {
			REX::WARN("AutoWalk: Stuck - no progress after {} frames", STUCK_FRAME_LIMIT);
			SpeechManager::GetSingleton()->Speak("Can't reach target");
			Stop();
		}
	}
}
