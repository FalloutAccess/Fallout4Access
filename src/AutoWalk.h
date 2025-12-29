#pragma once

#include "pch.h"
#include <functional>

class AutoWalk
{
public:
    static AutoWalk* GetSingleton();

    // Start walking to a target reference
    // a_onArrival callback is called when player arrives
    void WalkTo(RE::TESObjectREFR* a_target, float a_stopDistance = 100.0f,
                std::function<void()> a_onArrival = nullptr);

    // Stop walking immediately
    void Stop();

    // Check if currently walking
    bool IsWalking() const { return m_isWalking; }

    // Called frequently to update movement (register with input handler)
    void Update();

private:
    AutoWalk() = default;
    ~AutoWalk() = default;
    AutoWalk(const AutoWalk&) = delete;
    AutoWalk& operator=(const AutoWalk&) = delete;

    void OnArrived();

    // State
    bool m_isWalking = false;
    RE::TESObjectREFR* m_targetRef = nullptr;
    RE::NiPoint3 m_targetPos;  // Cached target position for arrival check
    float m_stopDistance = 100.0f;
    std::function<void()> m_onArrival = nullptr;

    // Constants
    static constexpr float ARRIVAL_TOLERANCE = 20.0f;
};
