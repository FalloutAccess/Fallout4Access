#include "GameplayStateAnnouncer.h"
#include "SpeechManager.h"
#include <RE/P/PlayerCamera.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/T/TESEnterSneakingEvent.h>
#include <Scaleform/G/GFx_Value.h>

GameplayStateAnnouncer* GameplayStateAnnouncer::GetSingleton()
{
	static GameplayStateAnnouncer singleton;
	return &singleton;
}

void GameplayStateAnnouncer::Register()
{
	auto* source = RE::TESEnterSneakingEvent::GetEventSource();
	if (source) {
		source->RegisterSink(this);
		REX::INFO("GameplayStateAnnouncer: Registered TESEnterSneakingEvent sink");
	}
}

void GameplayStateAnnouncer::Update()
{
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player)
		return;

	// Skip if in menu mode
	auto* ui = RE::UI::GetSingleton();
	if (ui && ui->menuMode)
		return;

	CheckCameraState();
	CheckSneakState(player);

	// Throttle detection polling - not every input event
	if (++m_updateTick % DETECTION_CHECK_INTERVAL == 0) {
		CheckDetectionState(player);
	}
}

RE::BSEventNotifyControl GameplayStateAnnouncer::ProcessEvent(
	const RE::TESEnterSneakingEvent& a_event,
	RE::BSTEventSource<RE::TESEnterSneakingEvent>*)
{
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player)
		return RE::BSEventNotifyControl::kContinue;

	// Only care about the player entering sneak
	if (a_event.actor.get() == player) {
		if (!m_wasSneaking.load(std::memory_order_relaxed)) {
			m_wasSneaking.store(true, std::memory_order_relaxed);
			SpeechManager::GetSingleton()->Speak("Sneaking");
		}
	}

	return RE::BSEventNotifyControl::kContinue;
}

void GameplayStateAnnouncer::CheckCameraState()
{
	auto* camera = RE::PlayerCamera::GetSingleton();
	if (!camera || !camera->currentState)
		return;

	auto currentState = camera->currentState->id.get();

	if (!m_cameraInitialized) {
		m_lastCameraState = currentState;
		m_cameraInitialized = true;
		return;
	}

	if (currentState == m_lastCameraState)
		return;

	m_lastCameraState = currentState;

	switch (currentState) {
		case RE::CameraState::kFirstPerson:
			SpeechManager::GetSingleton()->Speak("First person");
			break;
		case RE::CameraState::k3rdPerson:
			SpeechManager::GetSingleton()->Speak("Third person");
			break;
		case RE::CameraState::kFree:
			SpeechManager::GetSingleton()->Speak("Free camera");
			break;
		default:
			break;
	}
}

void GameplayStateAnnouncer::CheckSneakState(RE::PlayerCharacter* a_player)
{
	bool isSneaking = a_player->IsSneaking();

	// Detect sneak exit (enter is handled by TESEnterSneakingEvent)
	if (m_wasSneaking.load(std::memory_order_relaxed) && !isSneaking) {
		m_wasSneaking.store(false, std::memory_order_relaxed);
		m_lastStealthText.clear();
		SpeechManager::GetSingleton()->Speak("Standing");
	}
	// Sync state if somehow out of sync (e.g. game loaded while sneaking)
	else if (!m_wasSneaking.load(std::memory_order_relaxed) && isSneaking) {
		m_wasSneaking.store(true, std::memory_order_relaxed);
		m_lastStealthText.clear();
	}
}

void GameplayStateAnnouncer::CheckDetectionState(RE::PlayerCharacter*)
{
	if (!m_wasSneaking.load(std::memory_order_relaxed))
		return;

	// Read the stealth text directly from the HUD Scaleform element
	// This gives us exactly what's displayed on screen, properly localized
	auto* ui = RE::UI::GetSingleton();
	if (!ui)
		return;

	auto hudMenu = ui->GetMenu("HUDMenu");
	if (!hudMenu || !hudMenu->uiMovie)
		return;

	Scaleform::GFx::Value val;
	if (!hudMenu->uiMovie->GetVariable(&val, STEALTH_TEXT_PATH) || !val.IsString())
		return;

	std::string text = val.GetString();
	if (text.empty() || text == m_lastStealthText)
		return;

	m_lastStealthText = text;
	SpeechManager::GetSingleton()->Speak(text);
}
