#pragma once
#include <string>
#include <cstdint>

// Lightweight item identity for duplicate detection and staleness checks
// Used by DeferredItemAnnouncer to verify item hasn't changed during deferral
struct ItemIdentity {
    std::string name;
    std::int32_t count = 1;
    std::int32_t equipState = 0;
    bool isLegendary = false;
    bool isFavorite = false;
    bool isTagged = false;

    bool operator==(const ItemIdentity& other) const
    {
        // Primary identity is name - count/equip can change on same item
        return name == other.name;
    }

    bool operator!=(const ItemIdentity& other) const
    {
        return !(*this == other);
    }

    bool IsValid() const { return !name.empty(); }

    void Clear()
    {
        name.clear();
        count = 1;
        equipState = 0;
        isLegendary = false;
        isFavorite = false;
        isTagged = false;
    }

    // Build basic announcement (name + status flags, no stats)
    std::string BuildBasicAnnouncement() const
    {
        std::string result = name;

        if (count > 1) {
            result += ", " + std::to_string(count);
        }

        if (equipState > 0) {
            result += ", equipped";
        }

        if (isLegendary) {
            result += ", legendary";
        }

        if (isFavorite) {
            result += ", favorite";
        }

        if (isTagged) {
            result += ", tagged";
        }

        return result;
    }
};
