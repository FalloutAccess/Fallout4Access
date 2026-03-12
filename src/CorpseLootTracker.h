#pragma once

#include "pch.h"
#include <unordered_set>

// Tracks which corpses the player has viewed (opened container menu) or looted (quick-loot).
// Used by ObjectScanner to provide Unlooted/Looted subcategory filtering for Corpses.
// Persists across saves via F4SE cosave serialization.
class CorpseLootTracker :
	public RE::BSTEventSink<RE::TESContainerChangedEvent>
{
public:
	static CorpseLootTracker* GetSingleton();

	// Mark a corpse as looted by its FormID
	void MarkLooted(std::uint32_t a_formID);

	// Check if a corpse has been looted
	bool IsLooted(std::uint32_t a_formID) const;

	// Clear all tracked state (called on save revert)
	void Clear();

	// Register the event sink for TESContainerChangedEvent
	void Register();

	// F4SE cosave callbacks
	static void OnSave(const F4SE::SerializationInterface* a_intfc);
	static void OnLoad(const F4SE::SerializationInterface* a_intfc);
	static void OnRevert(const F4SE::SerializationInterface* a_intfc);

	// Cosave constants
	static constexpr std::uint32_t kUniqueID = 'F4AC';
	static constexpr std::uint32_t kRecordType = 'CLOT';
	static constexpr std::uint32_t kRecordVersion = 1;

private:
	CorpseLootTracker() = default;
	~CorpseLootTracker() override = default;
	CorpseLootTracker(const CorpseLootTracker&) = delete;
	CorpseLootTracker(CorpseLootTracker&&) = delete;
	CorpseLootTracker& operator=(const CorpseLootTracker&) = delete;
	CorpseLootTracker& operator=(CorpseLootTracker&&) = delete;

	// BSTEventSink<TESContainerChangedEvent>
	RE::BSEventNotifyControl ProcessEvent(
		const RE::TESContainerChangedEvent&               a_event,
		RE::BSTEventSource<RE::TESContainerChangedEvent>* a_source) override;

	std::unordered_set<std::uint32_t> m_lootedCorpses;
	mutable RE::BSSpinLock            m_lock;
};
