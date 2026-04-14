#pragma once

#include "pch.h"
#include <functional>

/**
 * AutoWalk - Controls automatic walking to scanner-selected objects.
 *
 * Communicates with Papyrus script F4A:AutoWalk via DispatchMethodCall,
 * passing FormIDs as proper int32 arguments (no float precision loss).
 *
 * Requires: fallout4access.esp with F4A_AutoWalkQuest and associated records.
 */
class AutoWalk
{
public:
	static AutoWalk* GetSingleton();

	// Initialize - find quest form (call after data is loaded)
	void Initialize();

	// Start walking to a target reference
	// Calls Papyrus OnWalkToTarget with FormID as int32
	void WalkTo(RE::TESObjectREFR* a_target, float a_stopDistance = 100.0f,
		std::function<void()> a_onArrival = nullptr);

	// Stop walking immediately (clears target)
	void Stop();

	// Check if currently walking (based on target being set)
	bool IsWalking() const { return m_targetRef != nullptr; }

	// Called frequently to check status
	void Update();

	// Number of cancel keys polled by Update(). Public so the cpp-side
	// table can static_assert it matches the latch array size.
	static constexpr std::size_t CANCEL_KEY_COUNT = 7;

private:
	AutoWalk() = default;
	~AutoWalk() = default;
	AutoWalk(const AutoWalk&) = delete;
	AutoWalk& operator=(const AutoWalk&) = delete;

	void CheckArrival();

	// Quest for Papyrus VM communication
	RE::TESQuest* m_quest = nullptr;

	// State tracking (for arrival callback)
	RE::TESObjectREFR*    m_targetRef = nullptr;
	float                 m_stopDistance = 100.0f;
	std::function<void()> m_onArrival = nullptr;

	// Stuck detection
	float m_lastDistance = 0.0f;
	int   m_stuckFrames = 0;

	// Cancel-key edge detection: true for each cancel key that was already
	// held when WalkTo() started. Those keys must be released at least once
	// before they're allowed to trigger a cancel, so that sharing a modifier
	// with the autowalk hotkey (e.g. Shift+E when Shift == Sprint) doesn't
	// stop us instantly on the first Update() tick.
	bool m_cancelKeyLatched[CANCEL_KEY_COUNT] = {};

	// Initialization state
	bool m_initialized = false;

	// Constants
	static constexpr float ARRIVAL_TOLERANCE = 30.0f;
	static constexpr int   STUCK_FRAME_LIMIT = 300;    // ~5 seconds at 60fps
	static constexpr float PROGRESS_THRESHOLD = 5.0f;  // minimum distance decrease to count as progress
};
