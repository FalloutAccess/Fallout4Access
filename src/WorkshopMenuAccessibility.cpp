#include "WorkshopMenuAccessibility.h"
#include "LocalizationHelper.h"
#include "ObjectScanner.h"
#include "SpeechManager.h"

WorkshopMenuAccessibility* WorkshopMenuAccessibility::GetSingleton()
{
    static WorkshopMenuAccessibility singleton;
    return &singleton;
}

void WorkshopMenuAccessibility::Install()
{
    if (m_installed) {
        return;
    }

    // Hook WorkshopMenu::AdvanceMovie (vtable index 4)
    static REL::HookVFT<void(RE::WorkshopMenu*, float, std::uint64_t)> hook(
        VTABLE::WorkshopMenu[0],
        4,
        HookedAdvanceMovie
    );
    if (hook.Enable()) {
        s_advanceMovieHook = &hook;
        REX::INFO("WorkshopMenuAccessibility: AdvanceMovie hook installed");
    } else {
        REX::ERROR("WorkshopMenuAccessibility: Failed to install AdvanceMovie hook");
    }

    m_installed = true;
}

void WorkshopMenuAccessibility::OnWorkshopMenuOpened()
{
    m_menuOpen = true;
    m_lastDisplayName.clear();
    m_lastDisplayPath.clear();
    m_lastInEditMode = false;
    m_lastSpokenText.clear();
    m_frameDelay = FRAME_DELAY_AFTER_OPEN;

    REX::INFO("WorkshopMenuAccessibility: Menu opened");

    // Register for workshop events
    if (!m_eventsRegistered) {
        RE::Workshop::RegisterForWorkshopModeEvent(this);
        RE::Workshop::RegisterForItemPlaced(this);
        RE::Workshop::RegisterForItemDestroyed(this);
        m_eventsRegistered = true;
    }

    // Switch scanner to workshop mode
    ObjectScanner::GetSingleton()->SetWorkshopMode(true);

    // Defer initial announcement
    if (auto* taskInterface = F4SE::GetTaskInterface()) {
        taskInterface->AddTask([this]() {
            if (!m_menuOpen) return;
            SpeakText("Workshop");
        });
    }
}

void WorkshopMenuAccessibility::OnWorkshopMenuClosed()
{
    m_menuOpen = false;
    REX::INFO("WorkshopMenuAccessibility: Menu closed");

    // Restore scanner to normal mode
    ObjectScanner::GetSingleton()->SetWorkshopMode(false);

    // Unregister event sinks
    if (m_eventsRegistered) {
        RE::Workshop::UnregisterForWorkshopModeEvent(this);
        RE::Workshop::UnregisterForItemPlaced(this);
        RE::Workshop::UnregisterForItemDestroyed(this);
        m_eventsRegistered = false;
    }
}

RE::BSEventNotifyControl WorkshopMenuAccessibility::ProcessEvent(
    const RE::Workshop::WorkshopModeEvent& a_event,
    RE::BSTEventSource<RE::Workshop::WorkshopModeEvent>*)
{
    if (a_event.start) {
        REX::INFO("WorkshopMenuAccessibility: Workshop mode started");
    } else {
        REX::INFO("WorkshopMenuAccessibility: Workshop mode ended");
    }
    return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl WorkshopMenuAccessibility::ProcessEvent(
    const RE::Workshop::ItemPlacedEvent& a_event,
    RE::BSTEventSource<RE::Workshop::ItemPlacedEvent>*)
{
    if (a_event.placedItem) {
        auto* baseObj = a_event.placedItem->GetObjectReference();
        if (baseObj) {
            auto fullName = RE::TESFullName::GetFullName(*baseObj);
            if (!fullName.empty()) {
                SpeakText("Placed " + std::string(fullName));
            } else {
                SpeakText("Item placed");
            }
        } else {
            SpeakText("Item placed");
        }
    }
    return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl WorkshopMenuAccessibility::ProcessEvent(
    const RE::Workshop::ItemDestroyedEvent& a_event,
    RE::BSTEventSource<RE::Workshop::ItemDestroyedEvent>*)
{
    if (a_event.objectDestroyed) {
        auto* baseObj = a_event.objectDestroyed->GetObjectReference();
        if (baseObj) {
            auto fullName = RE::TESFullName::GetFullName(*baseObj);
            if (!fullName.empty()) {
                SpeakText("Scrapped " + std::string(fullName));
                return RE::BSEventNotifyControl::kContinue;
            }
        }
    }
    SpeakText("Item scrapped");
    return RE::BSEventNotifyControl::kContinue;
}

void WorkshopMenuAccessibility::HookedAdvanceMovie(RE::WorkshopMenu* a_this, float a_timeDelta, std::uint64_t a_time)
{
    if (s_advanceMovieHook) {
        (*s_advanceMovieHook)(a_this, a_timeDelta, a_time);
    }

    auto* hook = GetSingleton();
    if (hook->m_menuOpen) {
        hook->CheckStateChanges(a_this);
    }
}

void WorkshopMenuAccessibility::CheckStateChanges(RE::WorkshopMenu* a_menu)
{
    if (!a_menu) return;

    // Wait for initial frame delay
    if (m_frameDelay > 0) {
        m_frameDelay--;
        return;
    }

    CheckEditModeChange(a_menu);
    CheckNavigationChange(a_menu);
}

void WorkshopMenuAccessibility::CheckNavigationChange(RE::WorkshopMenu* a_menu)
{
    std::string currentName = GetDisplayName(a_menu);
    std::string currentPath = GetDisplayPath(a_menu);

    // Only announce when BOTH name and path are stable and at least one changed
    if (currentName == m_lastDisplayName && currentPath == m_lastDisplayPath) {
        return;
    }

    // Don't announce empty states
    if (currentName.empty() && currentPath.empty()) {
        return;
    }

    m_lastDisplayName = currentName;
    m_lastDisplayPath = currentPath;

    // Build combined announcement
    std::string announcement;

    // Add path if non-empty (category breadcrumb)
    if (!currentPath.empty()) {
        announcement = currentPath;
    }

    // Add item name
    if (!currentName.empty()) {
        if (!announcement.empty()) {
            announcement += ". ";
        }
        announcement += currentName;
    }

    // Add details only for leaf items (when we have a name)
    if (!currentName.empty()) {
        // Add description if its parent container is visible
        if (IsMovieClipVisible(a_menu, DESCRIPTION_MC_PATH)) {
            std::string description = GetDescription(a_menu);
            if (!description.empty()) {
                announcement += ". " + description;
            }
        }

        // Add icon card info if its parent container is visible
        if (IsMovieClipVisible(a_menu, ICON_CARD_MC_PATH)) {
            std::string iconInfo = GetIconCardInfo(a_menu);
            if (!iconInfo.empty()) {
                announcement += ". " + iconInfo;
            }
        }

        // Add material requirements if container is visible
        if (IsMovieClipVisible(a_menu, REQUIREMENTS_MC_PATH)) {
            std::string requirements = GetRequirements(a_menu);
            if (!requirements.empty()) {
                announcement += ". " + requirements;
            }
        }

        // Add perk requirements if either perk panel is visible
        std::string perks = GetPerkRequirements(a_menu);
        if (!perks.empty()) {
            announcement += ". " + perks;
        }

        // Add built count if non-empty
        std::string builtCount = GetBuiltItemCount(a_menu);
        if (!builtCount.empty()) {
            announcement += ". Built: " + builtCount;
        }
    }

    SpeakText(announcement);
}

void WorkshopMenuAccessibility::CheckEditModeChange(RE::WorkshopMenu* a_menu)
{
    bool currentEditMode = a_menu->inEditMode;
    if (currentEditMode == m_lastInEditMode) {
        return;
    }
    m_lastInEditMode = currentEditMode;

    if (currentEditMode) {
        // Entering placement/edit mode
        std::string itemName = GetDisplayName(a_menu);
        if (!itemName.empty()) {
            SpeakText("Placing " + itemName);
        } else {
            SpeakText("Edit mode");
        }
        // Clear last display name so it re-announces when returning to browse
        m_lastDisplayName.clear();
    } else {
        SpeakText("Browse mode");
        // Reset so we re-announce the current selection
        m_lastDisplayName.clear();
        m_lastDisplayPath.clear();
    }
}

bool WorkshopMenuAccessibility::IsMovieClipVisible(RE::WorkshopMenu* a_menu, const char* a_mcPath)
{
    if (!a_menu || !a_menu->uiMovie) return false;

    // Get the MovieClip object
    Scaleform::GFx::Value mc;
    if (!a_menu->uiMovie->GetVariable(&mc, a_mcPath) || !mc.IsObject()) {
        return false;
    }

    // Check its .visible property
    Scaleform::GFx::Value visible;
    if (!mc.GetMember("visible", &visible) || !visible.IsBoolean()) {
        return false;
    }

    return visible.GetBoolean();
}

std::string WorkshopMenuAccessibility::GetDisplayName(RE::WorkshopMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) return "";

    Scaleform::GFx::Value val;
    if (a_menu->uiMovie->GetVariable(&val, ITEM_NAME_PATH) && val.IsString()) {
        return LocalizationHelper::CleanString(val.GetString());
    }
    return "";
}

std::string WorkshopMenuAccessibility::GetDisplayPath(RE::WorkshopMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) return "";

    Scaleform::GFx::Value val;
    if (a_menu->uiMovie->GetVariable(&val, DISPLAY_PATH_PATH) && val.IsString()) {
        return LocalizationHelper::CleanString(val.GetString());
    }
    return "";
}

std::string WorkshopMenuAccessibility::GetDescription(RE::WorkshopMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) return "";

    Scaleform::GFx::Value val;
    if (a_menu->uiMovie->GetVariable(&val, DESCRIPTION_PATH) && val.IsString()) {
        std::string desc = val.GetString();
        if (!desc.empty()) {
            return LocalizationHelper::CleanString(desc);
        }
    }
    return "";
}

std::string WorkshopMenuAccessibility::GetRequirements(RE::WorkshopMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) return "";

    Scaleform::GFx::Value entryList;
    if (!a_menu->uiMovie->GetVariable(&entryList, REQUIREMENTS_LIST_PATH) || !entryList.IsArray()) {
        return "";
    }

    std::uint32_t count = entryList.GetArraySize();
    if (count == 0) return "";

    std::string result = "Requires: ";
    bool first = true;

    for (std::uint32_t i = 0; i < count; i++) {
        Scaleform::GFx::Value entry;
        if (!entryList.GetElement(i, &entry) || !entry.IsObject()) continue;

        Scaleform::GFx::Value textVal;
        if (!entry.GetMember("text", &textVal) || !textVal.IsString()) continue;

        std::string text = textVal.GetString();
        if (text.empty()) continue;

        // Strip HTML tags (the SWF uses <font> tags for coloring)
        std::string cleanText;
        bool inTag = false;
        for (char c : text) {
            if (c == '<') { inTag = true; continue; }
            if (c == '>') { inTag = false; continue; }
            if (!inTag) cleanText += c;
        }

        // Add counts if available
        Scaleform::GFx::Value countsVal;
        if (entry.GetMember("counts", &countsVal) && countsVal.IsString()) {
            std::string counts = countsVal.GetString();
            if (!counts.empty()) {
                cleanText += " " + counts;
            }
        }

        if (!first) result += ", ";
        first = false;
        result += cleanText;
    }

    return first ? "" : result;
}

std::string WorkshopMenuAccessibility::ReadResourceIcon(RE::WorkshopMenu* a_menu, const char* a_iconPath)
{
    if (!a_menu || !a_menu->uiMovie) return "";

    Scaleform::GFx::Value icon;
    if (!a_menu->uiMovie->GetVariable(&icon, a_iconPath) || !icon.IsObject()) {
        return "";
    }

    // Check visibility
    Scaleform::GFx::Value visible;
    if (!icon.GetMember("visible", &visible) || !visible.IsBoolean() || !visible.GetBoolean()) {
        return "";
    }

    // Read currentFrame (1-indexed) to determine resource type
    Scaleform::GFx::Value frameVal;
    if (!icon.GetMember("currentFrame", &frameVal)) {
        return "";
    }

    int frame = 0;
    if (frameVal.IsInt()) frame = frameVal.GetInt();
    else if (frameVal.IsNumber()) frame = static_cast<int>(frameVal.GetNumber());
    else if (frameVal.IsUInt()) frame = static_cast<int>(frameVal.GetUInt());

    const char* resourceName = GetResourceNameFromFrame(frame);
    if (!resourceName) return "";

    std::string result = resourceName;

    // Read count from ResourceCount_tf if visible
    Scaleform::GFx::Value countTf;
    if (icon.GetMember("ResourceCount_tf", &countTf) && countTf.IsObject()) {
        Scaleform::GFx::Value countVisible;
        bool showCount = true;
        if (countTf.GetMember("visible", &countVisible) && countVisible.IsBoolean()) {
            showCount = countVisible.GetBoolean();
        }
        if (showCount) {
            Scaleform::GFx::Value countText;
            if (countTf.GetMember("text", &countText) && countText.IsString()) {
                std::string count = countText.GetString();
                if (!count.empty()) {
                    result += " " + count;
                }
            }
        }
    }

    return result;
}

const char* WorkshopMenuAccessibility::GetResourceNameFromFrame(int a_frame)
{
    // currentFrame is 1-indexed in Scaleform
    switch (a_frame) {
    case 1: return "Water";
    case 2: return "Power";
    case 3: return "People";
    case 4: return "Food";
    case 5: return "Defense";
    case 6: return "Happiness";
    case 7: return "Power";  // PowerCentered
    default: return nullptr;
    }
}

const char* WorkshopMenuAccessibility::GetAssignmentNameFromFrame(int a_frame)
{
    switch (a_frame) {
    case 1: return "Unassigned";
    case 2: return "Food";
    case 3: return "Safety";
    case 4: return "Provisioner";
    case 5: return "Scavenger";
    case 6: return "General vendor";
    case 7: return "Clinic vendor";
    case 8: return "Clothing vendor";
    case 9: return "Armor vendor";
    case 10: return "Weapons vendor";
    case 11: return "Bar vendor";
    case 12: return "Miscellaneous";
    default: return nullptr;
    }
}

std::string WorkshopMenuAccessibility::GetIconCardInfo(RE::WorkshopMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) return "";

    std::string info;

    // Check for assignment mode (mutually exclusive with requirement icons)
    Scaleform::GFx::Value assignIcon;
    if (a_menu->uiMovie->GetVariable(&assignIcon, ASSIGNMENT_ICON_PATH) && assignIcon.IsObject()) {
        Scaleform::GFx::Value assignVisible;
        if (assignIcon.GetMember("visible", &assignVisible) && assignVisible.IsBoolean() && assignVisible.GetBoolean()) {
            // Assignment mode — read assignment type from frame
            Scaleform::GFx::Value frameVal;
            if (assignIcon.GetMember("currentFrame", &frameVal)) {
                int frame = 0;
                if (frameVal.IsInt()) frame = frameVal.GetInt();
                else if (frameVal.IsNumber()) frame = static_cast<int>(frameVal.GetNumber());
                else if (frameVal.IsUInt()) frame = static_cast<int>(frameVal.GetUInt());

                const char* assignName = GetAssignmentNameFromFrame(frame);
                if (assignName) {
                    // Read the label text (shows "ASSIGNED TO" or "UNASSIGNED")
                    Scaleform::GFx::Value assignText;
                    if (a_menu->uiMovie->GetVariable(&assignText, ASSIGNED_TEXT_PATH) && assignText.IsString()) {
                        info = LocalizationHelper::CleanString(assignText.GetString());
                        info += ": " + std::string(assignName);
                    } else {
                        info = std::string(assignName);
                    }
                }
            }
            return info;
        }
    }

    // Requirement icons (power, people)
    std::string req1 = ReadResourceIcon(a_menu, REQUIRES_ICON1_PATH);
    std::string req2 = ReadResourceIcon(a_menu, REQUIRES_ICON2_PATH);

    if (!req1.empty() || !req2.empty()) {
        info = "Requires: ";
        if (!req1.empty()) {
            info += req1;
        }
        if (!req2.empty()) {
            if (!req1.empty()) info += ", ";
            info += req2;
        }
    }

    // Produces icons (food, water, power, defense, happiness)
    std::string prod1 = ReadResourceIcon(a_menu, PRODUCES_ICON1_PATH);
    std::string prod2 = ReadResourceIcon(a_menu, PRODUCES_ICON2_PATH);

    if (!prod1.empty() || !prod2.empty()) {
        if (!info.empty()) info += ". ";
        info += "Produces: ";
        if (!prod1.empty()) {
            info += prod1;
        }
        if (!prod2.empty()) {
            if (!prod1.empty()) info += ", ";
            info += prod2;
        }
    }

    return info;
}

std::string WorkshopMenuAccessibility::GetPerkRequirements(RE::WorkshopMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) return "";

    std::string perks;

    // Check perk panel 0 only if visible
    if (IsMovieClipVisible(a_menu, PERK0_MC_PATH)) {
        Scaleform::GFx::Value perk0;
        if (a_menu->uiMovie->GetVariable(&perk0, PERK0_NAME_PATH) && perk0.IsString()) {
            std::string perkName = perk0.GetString();
            if (!perkName.empty()) {
                perks = "Requires perk: " + LocalizationHelper::CleanString(perkName);
            }
        }
    }

    // Check perk panel 1 only if visible
    if (IsMovieClipVisible(a_menu, PERK1_MC_PATH)) {
        Scaleform::GFx::Value perk1;
        if (a_menu->uiMovie->GetVariable(&perk1, PERK1_NAME_PATH) && perk1.IsString()) {
            std::string perkName = perk1.GetString();
            if (!perkName.empty()) {
                if (perks.empty()) {
                    perks = "Requires perk: ";
                } else {
                    perks += ", ";
                }
                perks += LocalizationHelper::CleanString(perkName);
            }
        }
    }

    return perks;
}

std::string WorkshopMenuAccessibility::GetBuiltItemCount(RE::WorkshopMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) return "";

    Scaleform::GFx::Value val;
    if (a_menu->uiMovie->GetVariable(&val, BUILT_COUNT_PATH) && val.IsString()) {
        std::string count = val.GetString();
        // Only return if non-empty and contains non-whitespace
        if (!count.empty() && count.find_first_not_of(" \t\n\r") != std::string::npos) {
            return count;
        }
    }
    return "";
}

std::string WorkshopMenuAccessibility::GetStatValue(RE::WorkshopMenu* a_menu, const char* a_path)
{
    if (!a_menu || !a_menu->uiMovie) return "";

    Scaleform::GFx::Value val;
    if (a_menu->uiMovie->GetVariable(&val, a_path) && val.IsString()) {
        return val.GetString();
    }
    return "";
}

void WorkshopMenuAccessibility::AnnounceSettlementStats()
{
    if (!m_menuOpen) return;

    auto ui = RE::UI::GetSingleton();
    if (!ui) return;

    auto menu = ui->GetMenu("WorkshopMenu"sv);
    if (!menu) return;

    auto* workshopMenu = static_cast<RE::WorkshopMenu*>(menu.get());

    // Get FX targets for warning color detection
    RE::WorkshopMenu::FXWorkshopMenu* fx = nullptr;
    if (workshopMenu->workshopMenuBase) {
        fx = workshopMenu->workshopMenuBase.get();
    }

    std::string stats;

    auto addStat = [&](const char* label, const char* path, RE::BSGFxShaderFXTarget* fxTarget) {
        std::string value = GetStatValue(workshopMenu, path);
        if (!value.empty()) {
            if (!stats.empty()) stats += ", ";
            stats += std::string(label) + " " + value;
            // Check if this stat is in warning/red state (insufficient)
            if (fxTarget && fxTarget->HUDColorType.get() == RE::HUDColorTypes::kWarningColor) {
                stats += " low";
            }
        }
    };

    addStat("People", PEOPLE_VALUE_PATH, fx ? fx->happinessPeopleBase.get() : nullptr);
    addStat("Food", FOOD_VALUE_PATH, fx ? fx->happinessFoodBase.get() : nullptr);
    addStat("Water", WATER_VALUE_PATH, fx ? fx->happinessWaterBase.get() : nullptr);
    addStat("Power", POWER_VALUE_PATH, fx ? fx->happinessPowerBase.get() : nullptr);
    addStat("Defense", SAFETY_VALUE_PATH, fx ? fx->happinessSafetyBase.get() : nullptr);
    addStat("Beds", BEDS_VALUE_PATH, fx ? fx->happinessBedsBase.get() : nullptr);
    addStat("Happiness", HAPPINESS_VALUE_PATH, fx ? fx->happinessHappyValue.get() : nullptr);

    if (!stats.empty()) {
        SpeechManager::GetSingleton()->Speak("Settlement: " + stats);
    } else {
        SpeechManager::GetSingleton()->Speak("No settlement stats available");
    }
}

void WorkshopMenuAccessibility::SpeakText(const std::string& a_text)
{
    if (a_text.empty()) return;

    // Deduplicate
    if (a_text == m_lastSpokenText) return;
    m_lastSpokenText = a_text;

    SpeechManager::GetSingleton()->Speak(a_text);
}
