#pragma once

#include "pch.h"
#include <optional>
#include <string>

// Accessibility for the Freedom Trail Ring puzzle at the Old North Church (quest RR101).
// The puzzle has three interactable FormIDs:
//   - RR101RingClockwiseSpinner        (0x00019B19) - rotates the ring clockwise
//   - RR101RingCounterClockwiseSpinner (0x00019B1F) - rotates the ring counter-clockwise
//   - RR101CenterRing                  (0x00108091) - submits the letter at the pointer
// The ring's current letter is derived from the Z-rotation of the puzzle ring ref
// RR101PuzzleRingActivator (0x00108090). The 21 letter positions spell
// "THE FREEDOM TRAIL BOSTON" around the dial, each tied to a specific Z-angle in degrees.
class FreedomTrailAccessibility :
	public RE::BSTEventSink<RE::TESActivateEvent>
{
public:
	static FreedomTrailAccessibility* GetSingleton();

	void Register();

	// Returns a suffix like ", current letter R" to append to the crosshair
	// announcement when a_ref is one of the ring's three activator FormIDs.
	// Returns std::nullopt for any other ref.
	std::optional<std::string> GetLetterSuffix(RE::TESObjectREFR* a_ref);

	// Event sink
	RE::BSEventNotifyControl ProcessEvent(
		const RE::TESActivateEvent&               a_event,
		RE::BSTEventSource<RE::TESActivateEvent>* a_source) override;

private:
	FreedomTrailAccessibility() = default;
	~FreedomTrailAccessibility() override = default;
	FreedomTrailAccessibility(const FreedomTrailAccessibility&) = delete;
	FreedomTrailAccessibility(FreedomTrailAccessibility&&) = delete;
	FreedomTrailAccessibility& operator=(const FreedomTrailAccessibility&) = delete;
	FreedomTrailAccessibility& operator=(FreedomTrailAccessibility&&) = delete;

	enum class RingRole
	{
		None,
		RotateClockwise,
		RotateCounterClockwise,
		Submit,
	};

	RingRole ClassifyRef(RE::TESObjectREFR* a_ref) const;

	// Reads the puzzle ring ref's current Z-angle and returns the letter at the
	// pointer (or '\0' if the ref isn't loaded / angle doesn't snap to a marker).
	char GetCurrentLetter() const;

	bool m_registered = false;
};
