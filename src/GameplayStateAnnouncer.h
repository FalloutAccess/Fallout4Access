#pragma once

#include "pch.h"
#include <atomic>

class GameplayStateAnnouncer :
	public RE::BSTEventSink<RE::TESEnterSneakingEvent>
{
public:
	static GameplayStateAnnouncer* GetSingleton();

	void Register();
	void Update();  // Called from ObjectScanner input handler

private:
	GameplayStateAnnouncer() = default;
	~GameplayStateAnnouncer() override = default;
	GameplayStateAnnouncer(const GameplayStateAnnouncer&) = delete;
	GameplayStateAnnouncer(GameplayStateAnnouncer&&) = delete;
	GameplayStateAnnouncer& operator=(const GameplayStateAnnouncer&) = delete;
	GameplayStateAnnouncer& operator=(GameplayStateAnnouncer&&) = delete;

	// BSTEventSink<TESEnterSneakingEvent>
	RE::BSEventNotifyControl ProcessEvent(
		const RE::TESEnterSneakingEvent&               a_event,
		RE::BSTEventSource<RE::TESEnterSneakingEvent>* a_source) override;

	// State checks
	void CheckCameraState();
	void CheckSneakState(RE::PlayerCharacter* a_player);
	void CheckDetectionState(RE::PlayerCharacter* a_player);

	// Camera state
	RE::CameraState m_lastCameraState = RE::CameraState::kFirstPerson;
	bool            m_cameraInitialized = false;

	// Sneak state (atomic: written by event thread, read by input thread)
	std::atomic<bool> m_wasSneaking = false;

	// Detection state - tracks the stealth text shown on HUD
	std::string m_lastStealthText;

	// Throttle detection polling (called on every input event)
	std::uint32_t                  m_updateTick = 0;
	static constexpr std::uint32_t DETECTION_CHECK_INTERVAL = 10;

	// HUD stealth meter Scaleform path
	static constexpr const char* STEALTH_TEXT_PATH =
		"root.TopCenterGroup_mc.StealthMeter_mc.StealthTextInstance.text";
};
