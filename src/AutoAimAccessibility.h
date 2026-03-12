#pragma once
#include "pch.h"
#include <RE/B/BGSSoundDescriptorForm.h>

class AutoAimAccessibility
{
public:
	static AutoAimAccessibility* GetSingleton();

	void Initialize();
	void Update();  // Called from ObjectScanner input handler

	void SetEnabled(bool a_enabled) { m_enabled = a_enabled; }
	bool IsEnabled() const { return m_enabled; }
	void SetLOSSoundInstanceID(std::uint32_t a_id);

private:
	AutoAimAccessibility() = default;
	~AutoAimAccessibility() = default;
	AutoAimAccessibility(const AutoAimAccessibility&) = delete;
	AutoAimAccessibility& operator=(const AutoAimAccessibility&) = delete;

	void       OnEnterIronsights();
	RE::Actor* FindNearestHostileActor();
	void       RotateToTarget(RE::Actor* a_target);
	void       AnnounceTarget(RE::Actor* a_target);

	bool  IsValidTarget(RE::Actor* a_actor);
	float CalculateDistanceToActor(RE::Actor* a_actor);

	// Target tracking methods
	void LockTarget(RE::Actor* a_target);
	void StopTracking();
	bool IsLockedTargetValid();
	void UpdateTracking();  // Called from Update() while in ironsights

	// LOS checking
	bool  HasLineOfSight(RE::Actor* a_target);
	float CalculateTargetScore(RE::Actor* a_actor);  // Distance with LOS penalty

	// Audio
	void StartLOSSound();
	void StopLOSSound();
	void StopSoundInstance(std::uint32_t a_instanceID);

	RE::GUN_STATE m_lastGunState = RE::GUN_STATE::kRelaxed;
	bool          m_enabled = true;
	bool          m_initialized = false;

	// Tracking state
	RE::ActorHandle m_lockedTargetHandle;      // Safe handle to locked target
	bool            m_isTracking = false;      // Whether actively tracking
	std::uint32_t   m_trackingFrameCount = 0;  // Frame counter for rate limiting

	// LOS audio state (Papyrus VM playback)
	RE::BGSSoundDescriptorForm* m_losSoundForm = nullptr;
	std::uint32_t               m_losSoundInstanceID = 0;
	bool                        m_losSoundPlaying = false;
	bool                        m_pendingStop = false;  // Stop requested before callback arrived
	bool                        m_targetHasLOS = false;

	static constexpr float         MAX_TARGET_DISTANCE = 5000.0f;
	static constexpr std::uint32_t TRACK_UPDATE_INTERVAL = 60;  // ~1 update/sec at 60fps
	static constexpr float         NO_LOS_PENALTY = 3.0f;       // 3x distance for no-LOS targets
};
