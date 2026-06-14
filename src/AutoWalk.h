#pragma once

#include "pch.h"
#include <functional>

/**
 * AutoWalk - Controls automatic walking to scanner-selected objects.
 *
 * Drives the player via the loose global Papyrus script F4A:AutoWalkHelper
 * (DispatchStaticCall), passing the target FormID as an int32. The helper uses
 * the latent native Actor.PathToReference, so the dispatch MUST supply a
 * non-null IStackCallbackFunctor: the VM's latent-return path dereferences the
 * stack callback without a null check, unlike the inline path used by
 * non-latent calls (e.g. F4A:SoundHelper.PlaySound, which can pass nullptr).
 *
 * No ESP required: F4A:AutoWalkHelper is a global-function-only script the VM
 * lazy-loads by name (same as F4A:SoundHelper). This lets auto-walk work in
 * load orders where our quest never binds, e.g. Fallout London.
 */
class AutoWalk
{
public:
	static AutoWalk* GetSingleton();

	// Initialize - mark the feature ready (call after data is loaded)
	void Initialize();

	// Start walking to a target reference.
	// Dispatches F4A:AutoWalkHelper.WalkPlayerTo(formID).
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

	// Tell the helper script to halt the in-progress (latent) walk and hand
	// control back to the player. Shared by Stop() and arrival detection.
	void DispatchStopWalk();

	// State tracking (for arrival callback)
	RE::TESObjectREFR*    m_targetRef = nullptr;
	float                 m_stopDistance = 100.0f;
	std::function<void()> m_onArrival = nullptr;

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
};
