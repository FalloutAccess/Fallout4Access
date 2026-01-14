#pragma once

#include "pch.h"
#include <functional>

/**
 * AutoWalk - Controls automatic walking to scanner-selected objects.
 *
 * Communicates with Papyrus script F4A:AutoWalk via GlobalVariables:
 * - F4A_TargetFormID: C++ writes target FormID here, Papyrus reads and walks
 * - F4A_StopDistance: How close to stop (default 100 units)
 *
 * Requires: fallout4access.esp with F4A_AutoWalkQuest and associated records.
 */
class AutoWalk
{
public:
    static AutoWalk* GetSingleton();

    // Initialize - find GlobalVariables (call after data is loaded)
    void Initialize();

    // Start walking to a target reference
    // Sets the GlobalVariable for Papyrus to pick up
    void WalkTo(RE::TESObjectREFR* a_target, float a_stopDistance = 100.0f,
                std::function<void()> a_onArrival = nullptr);

    // Stop walking immediately (clears target)
    void Stop();

    // Check if currently walking (based on target being set)
    bool IsWalking() const { return m_targetRef != nullptr; }

    // Called frequently to check status
    void Update();

private:
    AutoWalk() = default;
    ~AutoWalk() = default;
    AutoWalk(const AutoWalk&) = delete;
    AutoWalk& operator=(const AutoWalk&) = delete;

    void CheckArrival();

    // GlobalVariables for Papyrus communication
    RE::TESGlobal* m_globalTargetFormID = nullptr;
    RE::TESGlobal* m_globalStopDistance = nullptr;

    // State tracking (for arrival callback)
    RE::TESObjectREFR* m_targetRef = nullptr;
    float m_stopDistance = 100.0f;
    std::function<void()> m_onArrival = nullptr;

    // Initialization state
    bool m_initialized = false;

    // Constants
    static constexpr float ARRIVAL_TOLERANCE = 30.0f;
};
