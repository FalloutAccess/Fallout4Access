#pragma once
#include "pch.h"
#include "ItemIdentity.h"
#include "ItemStatFormatter.h"
#include <Scaleform/G/GFx_Value.h>

// Handles deferred item announcement with identity verification
// Solves the InfoObj lag problem by deferring announcement by 1 frame
//
// When a user navigates in Pip-Boy inventory:
// - Frame N: selectedEntry updates to new item, but InfoObj still has old item's stats
// - Frame N+1: InfoObj updates to match selectedEntry
//
// This class defers the stat announcement to frame N+1, then verifies
// the item hasn't changed before announcing.
class DeferredItemAnnouncer {
public:
    // Schedule an announcement for the next frame
    // Will verify identity matches before speaking stats
    void ScheduleAnnouncement(const ItemIdentity& a_identity);

    // Reset state (e.g., menu closed or page changed)
    void Reset();

private:
    // Execute the deferred announcement (called on next frame)
    void ExecuteAnnouncement(const ItemIdentity& a_expectedIdentity);

    // Extract stats from ItemCard_mc.InfoObj
    std::vector<ItemStatFormatter::FormattedStat> ExtractCurrentStats();

    // Build full announcement from identity + stats
    std::string BuildAnnouncement(const ItemIdentity& a_identity,
                                  const std::vector<ItemStatFormatter::FormattedStat>& a_stats);

    void SpeakText(const std::string& a_text);

    // Clean localization string (remove $ prefix, replace underscores)
    static std::string CleanString(const std::string& a_text);

    ItemIdentity m_lastAnnounced;  // For duplicate prevention
};
