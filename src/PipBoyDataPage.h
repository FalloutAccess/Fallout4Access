#pragma once

#include "PipBoyPageBase.h"

// Handles the DATA page of the Pip-Boy
// Tabs: Quests(0), Workshops(1), Stats(2)
// Quest/workshop selection is handled by PipBoyCallbackHook callbacks.
// Tab changes are detected per-frame from the AdvanceMovie hook.
class PipBoyDataPage : public PipBoyPageBase
{
public:
    PipBoyDataPage() = default;
    ~PipBoyDataPage() override = default;

    // PipBoyPageBase interface
    void OnPageActivated() override;
    void OnPageDeactivated() override;
    void CheckSelection() override;
    void CheckTabChange() override;
    void Reset() override;

private:
    // Tab definitions
    enum class Tab {
        Quests = 0,
        Workshops = 1,
        Stats = 2
    };

    // Tab localization keys (ordered by tab index)
    static constexpr const char* kTabKeys[] = { "$QUESTS", "$SANCTUARY", "$LOG" };
    static constexpr int kTabCount = 3;

    std::string GetLocalizedTabName(std::int32_t a_tabIndex);
};
