#include "PipBoyMapPage.h"
#include "LocalizationHelper.h"
#include "SpeechManager.h"
#include <RE/P/PlayerCharacter.h>
#include <RE/M/MenuCursor.h>
#include <cmath>
#include <algorithm>

void PipBoyMapPage::OnPageActivated()
{
    REX::INFO("PipBoyMapPage: Activated");
    m_announced = false;
    m_lastSelection = -1;
    m_lastTab = -1;

    // Cache player position for distance calculations
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (player) {
        RE::NiPoint3 pos = player->GetPosition();
        m_playerX = pos.x;
        m_playerY = pos.y;
    }

    // Reset reference to player position when entering map
    m_useCustomReference = false;
    m_referenceX = m_playerX;
    m_referenceY = m_playerY;
    m_referenceName = "player";

    // Get menu and announce current tab
    auto ui = RE::UI::GetSingleton();
    if (!ui) return;

    auto menu = ui->GetMenu("PipboyMenu"sv);
    if (!menu) return;

    auto tab = GetCurrentTab(menu.get());
    if (tab >= 0) {
        m_lastTab = tab;
        m_isLocalMap = (tab == 1);
        SpeakText(GetLocalizedTabName(tab));
    }

    // Build marker list with a small delay to let Scaleform populate
    if (auto* taskInterface = F4SE::GetTaskInterface()) {
        taskInterface->AddTask([this]() {
            BuildMarkerList();
            if (!m_filteredIndices.empty()) {
                m_currentIndex = 0;
                AnnounceCurrentMarker();
            } else {
                SpeakText("No markers");
            }
        });
    }
}

void PipBoyMapPage::OnPageDeactivated()
{
    REX::INFO("PipBoyMapPage: Deactivated");
}

void PipBoyMapPage::CheckSelection()
{
    // Check for embedded message box first (fast travel confirmation etc.)
    CheckMessageBox();
    if (m_messageBoxOpen) return;

    // Poll selectedMarkerInfo from the map holder (uses mapPageObj, not root.Menu_mc)
    auto ui = RE::UI::GetSingleton();
    if (!ui) return;

    auto menu = ui->GetMenu("PipboyMenu"sv);
    if (!menu) return;

    auto* pipboyMenu = static_cast<RE::PipboyMenu*>(menu.get());
    const auto& mapPageObj = pipboyMenu->mapMenuObj.mapPageObj;
    if (!mapPageObj.IsObject()) return;

    const char* holderName = m_isLocalMap ? "LocalMapHolder_mc" : "WorldMapHolder_mc";
    Scaleform::GFx::Value holder;
    if (!const_cast<Scaleform::GFx::Value&>(mapPageObj).GetMember(holderName, &holder) || !holder.IsObject()) return;

    // Read selectedMarkerInfo - contains the marker currently under mouse cursor
    Scaleform::GFx::Value markerInfo;
    if (!holder.GetMember("selectedMarkerInfo", &markerInfo)) return;

    // Check if null/undefined (no marker under cursor)
    if (markerInfo.GetType() == Scaleform::GFx::Value::ValueType::kNull ||
        markerInfo.GetType() == Scaleform::GFx::Value::ValueType::kUndefined) {
        m_lastGameSelectedIndex = -1;
        return;
    }

    if (!markerInfo.IsObject()) return;

    // Extract arrayIndex from selectedMarkerInfo
    Scaleform::GFx::Value arrIdxVal;
    if (!markerInfo.GetMember("arrayIndex", &arrIdxVal)) return;

    std::uint32_t arrayIndex = UINT32_MAX;
    if (arrIdxVal.IsUInt()) arrayIndex = arrIdxVal.GetUInt();
    else if (arrIdxVal.IsNumber()) arrayIndex = static_cast<std::uint32_t>(arrIdxVal.GetNumber());
    else if (arrIdxVal.IsInt() && arrIdxVal.GetInt() >= 0) arrayIndex = static_cast<std::uint32_t>(arrIdxVal.GetInt());

    if (arrayIndex == UINT32_MAX) return;

    // Check if selection changed
    std::int32_t currentSel = static_cast<std::int32_t>(arrayIndex);
    if (currentSel == m_lastGameSelectedIndex) return;
    m_lastGameSelectedIndex = currentSel;

    // Find matching marker in our list and sync
    for (std::int32_t i = 0; i < static_cast<std::int32_t>(m_markers.size()); ++i) {
        if (m_markers[i].arrayIndex == arrayIndex) {
            for (std::int32_t fi = 0; fi < static_cast<std::int32_t>(m_filteredIndices.size()); ++fi) {
                if (m_filteredIndices[fi] == i) {
                    if (m_currentIndex != fi) {
                        m_currentIndex = fi;
                        AnnounceCurrentMarker(false, true);
                    }
                    return;
                }
            }
            break;
        }
    }
}

void PipBoyMapPage::CheckMessageBox()
{
    auto ui = RE::UI::GetSingleton();
    if (!ui) return;

    auto menu = ui->GetMenu("PipboyMenu"sv);
    if (!menu) return;

    auto* pipboyMenu = static_cast<RE::PipboyMenu*>(menu.get());
    const auto& mapPageObj = pipboyMenu->mapMenuObj.mapPageObj;
    if (!mapPageObj.IsObject()) return;

    // Check MessageHolder_mc visibility
    Scaleform::GFx::Value holder;
    if (!const_cast<Scaleform::GFx::Value&>(mapPageObj).GetMember("MessageHolder_mc", &holder) || !holder.IsObject()) {
        if (m_messageBoxOpen) {
            m_messageBoxOpen = false;
            m_messageBoxLastSelection = -1;
            m_messageBoxLastBodyText.clear();
            m_messageBoxLastButtonLabels.clear();
        }
        return;
    }

    Scaleform::GFx::Value visibleVal;
    bool isVisible = false;
    if (holder.GetMember("visible", &visibleVal) && visibleVal.IsBoolean()) {
        isVisible = visibleVal.GetBoolean();
    }

    if (!isVisible) {
        if (m_messageBoxOpen) {
            REX::INFO("CheckMessageBox: Message box closed");
            m_messageBoxOpen = false;
            m_messageBoxLastSelection = -1;
            m_messageBoxLastBodyText.clear();
            m_messageBoxLastButtonLabels.clear();
        }
        return;
    }

    // MessageHolder_mc is visible - find Menu_mc inside loaded PipboyMessageBox.swf
    // MessageHolder_mc has a pre-existing dimmer child (Symbol 223) at index 0;
    // the loaded SWF root is appended via addChild(), so iterate to find the child with Menu_mc
    Scaleform::GFx::Value numChildrenVal;
    if (!holder.GetMember("numChildren", &numChildrenVal)) return;
    std::uint32_t numChildren = 0;
    if (numChildrenVal.IsUInt()) numChildren = numChildrenVal.GetUInt();
    else if (numChildrenVal.IsNumber()) numChildren = static_cast<std::uint32_t>(numChildrenVal.GetNumber());
    else if (numChildrenVal.IsInt() && numChildrenVal.GetInt() > 0) numChildren = static_cast<std::uint32_t>(numChildrenVal.GetInt());

    Scaleform::GFx::Value menuMc;
    bool foundMenu = false;
    for (std::uint32_t i = 0; i < numChildren; ++i) {
        Scaleform::GFx::Value child;
        Scaleform::GFx::Value idx;
        idx = i;
        if (!holder.Invoke("getChildAt", &child, &idx, 1) || !child.IsObject()) continue;

        if (child.GetMember("Menu_mc", &menuMc) && menuMc.IsObject()) {
            foundMenu = true;
            break;
        }
    }
    if (!foundMenu) return;

    // Read current body text
    Scaleform::GFx::Value bodyVal;
    std::string currentBodyText;
    if (menuMc.GetMember("bodyText", &bodyVal) && bodyVal.IsString()) {
        currentBodyText = bodyVal.GetString();
    }

    // Read current button labels
    Scaleform::GFx::Value buttonArray;
    std::vector<std::string> currentButtonLabels;
    std::uint32_t buttonCount = 0;
    if (menuMc.GetMember("buttonArray", &buttonArray) && buttonArray.IsArray()) {
        buttonCount = buttonArray.GetArraySize();
        for (std::uint32_t i = 0; i < buttonCount; ++i) {
            Scaleform::GFx::Value btn;
            if (buttonArray.GetElement(i, &btn) && btn.IsObject()) {
                Scaleform::GFx::Value textVal;
                if (btn.GetMember("text", &textVal) && textVal.IsString()) {
                    currentButtonLabels.push_back(textVal.GetString());
                } else {
                    currentButtonLabels.push_back("");
                }
            } else {
                currentButtonLabels.push_back("");
            }
        }
    }

    // Read selected index
    Scaleform::GFx::Value selVal;
    std::int32_t selectedIndex = 0;
    if (menuMc.GetMember("selectedIndex", &selVal)) {
        if (selVal.IsUInt()) selectedIndex = static_cast<std::int32_t>(selVal.GetUInt());
        else if (selVal.IsInt()) selectedIndex = selVal.GetInt();
        else if (selVal.IsNumber()) selectedIndex = static_cast<std::int32_t>(selVal.GetNumber());
    }

    // Check if content changed (body text or button labels)
    bool contentChanged = (currentBodyText != m_messageBoxLastBodyText) ||
                          (currentButtonLabels != m_messageBoxLastButtonLabels);

    if (!m_messageBoxOpen || contentChanged) {
        // First open or content changed — announce full state
        if (!m_messageBoxOpen) {
            m_messageBoxOpen = true;
            REX::INFO("CheckMessageBox: Message box opened");
        }

        std::string announcement;
        std::string translatedBody = LocalizationHelper::Translate(currentBodyText);
        if (!translatedBody.empty()) {
            announcement = translatedBody;
        }
        if (buttonCount > 0) {
            if (!announcement.empty()) {
                announcement += ". ";
            }
            std::string buttonWord = (buttonCount == 1)
                ? LocalizationHelper::Translate("$F4A_button")
                : LocalizationHelper::Translate("$F4A_buttons");
            announcement += std::to_string(buttonCount) + " " + buttonWord;

            if (selectedIndex >= 0 && selectedIndex < static_cast<std::int32_t>(buttonCount)) {
                std::string selectedButton = LocalizationHelper::Translate(currentButtonLabels[selectedIndex]);
                if (!selectedButton.empty()) {
                    announcement += ". " + selectedButton;
                }
            }
        }

        if (!announcement.empty()) {
            SpeakText(announcement);
        }

        m_messageBoxLastBodyText = currentBodyText;
        m_messageBoxLastButtonLabels = currentButtonLabels;
        m_messageBoxLastSelection = selectedIndex;
    } else if (selectedIndex >= 0 && selectedIndex != m_messageBoxLastSelection) {
        // Content unchanged, but selection changed — announce just the button
        m_messageBoxLastSelection = selectedIndex;

        if (selectedIndex < static_cast<std::int32_t>(buttonCount)) {
            std::string buttonText = LocalizationHelper::Translate(currentButtonLabels[selectedIndex]);
            if (!buttonText.empty()) {
                SpeakText(buttonText);
            }
        }
    }
}

void PipBoyMapPage::CheckTabChange()
{
    auto ui = RE::UI::GetSingleton();
    if (!ui) return;

    auto menu = ui->GetMenu("PipboyMenu"sv);
    if (!menu) return;

    auto currentTab = GetCurrentTab(menu.get());

    // Tab 0 = World Map, Tab 1 = Local Map
    if (currentTab >= 0 && currentTab != m_lastTab) {
        m_lastTab = currentTab;
        m_isLocalMap = (currentTab == 1);

        // Reset selection tracking since we're on a different map
        m_currentIndex = -1;
        m_filteredIndices.clear();
        m_markers.clear();

        SpeakText(GetLocalizedTabName(currentTab));

        // Rebuild marker list for the new map
        if (auto* taskInterface = F4SE::GetTaskInterface()) {
            taskInterface->AddTask([this]() {
                BuildMarkerList();
                if (!m_filteredIndices.empty()) {
                    m_currentIndex = 0;
                    AnnounceCurrentMarker();
                } else {
                    SpeakText("No markers");
                }
            });
        }
    }
}

void PipBoyMapPage::Reset()
{
    m_announced = false;
    m_lastSelection = -1;
    m_lastTab = -1;
    m_currentIndex = -1;
    m_filter = MarkerFilter::All;
    m_isLocalMap = false;
    m_markers.clear();
    m_filteredIndices.clear();
    m_useCustomReference = false;
    m_referenceX = 0;
    m_referenceY = 0;
    m_referenceName.clear();
    m_lastGameSelectedIndex = -1;
    m_messageBoxOpen = false;
    m_messageBoxLastSelection = -1;
    m_messageBoxLastBodyText.clear();
    m_messageBoxLastButtonLabels.clear();
}

void PipBoyMapPage::OnNextMarker()
{
    if (m_filteredIndices.empty()) {
        SpeakText("No markers");
        return;
    }

    if (m_currentIndex >= static_cast<std::int32_t>(m_filteredIndices.size()) - 1) {
        SpeakText("Last marker");
        return;
    }

    m_currentIndex++;
    SelectMarkerAtIndex(m_currentIndex);
}

void PipBoyMapPage::OnPreviousMarker()
{
    if (m_filteredIndices.empty()) {
        SpeakText("No markers");
        return;
    }

    if (m_currentIndex <= 0) {
        SpeakText("First marker");
        return;
    }

    m_currentIndex--;
    SelectMarkerAtIndex(m_currentIndex);
}

void PipBoyMapPage::OnAnnounceDetails()
{
    if (m_currentIndex < 0 || m_filteredIndices.empty()) {
        SpeakText("No marker selected");
        return;
    }

    // Center the map on the current marker
    CenterOnCurrentMarker();

    AnnounceCurrentMarker(true);  // Full details
}

void PipBoyMapPage::CenterOnCurrentMarker()
{
    if (m_currentIndex < 0 || m_filteredIndices.empty()) return;

    auto ui = RE::UI::GetSingleton();
    if (!ui) return;

    auto menu = ui->GetMenu("PipboyMenu"sv);
    if (!menu || !menu->uiMovie) return;

    auto* pipboyMenu = static_cast<RE::PipboyMenu*>(menu.get());
    const auto& mapPageObj = pipboyMenu->mapMenuObj.mapPageObj;
    if (!mapPageObj.IsObject()) return;

    auto& marker = m_markers[m_filteredIndices[m_currentIndex]];

    // Get the map holder from mapPageObj (it's a child, not under root.Menu_mc)
    const char* holderName = m_isLocalMap ? "LocalMapHolder_mc" : "WorldMapHolder_mc";
    Scaleform::GFx::Value holder;
    if (!const_cast<Scaleform::GFx::Value&>(mapPageObj).GetMember(holderName, &holder) || !holder.IsObject()) return;

    // Check if map is loaded
    Scaleform::GFx::Value mapLoaded;
    if (holder.GetMember("mapLoaded", &mapLoaded) && mapLoaded.IsBoolean() && !mapLoaded.GetBoolean()) return;

    // Find positional index in markerArray (PUBLIC getter for MarkerInfoA)
    // Set visual selection via private backing fields
    Scaleform::GFx::Value markerArray;
    if (holder.GetMember("markerArray", &markerArray) && markerArray.IsArray()) {
        std::uint32_t arraySize = markerArray.GetArraySize();
        for (std::uint32_t i = 0; i < arraySize; ++i) {
            Scaleform::GFx::Value elem;
            if (!markerArray.GetElement(i, &elem) || !elem.IsObject()) continue;

            Scaleform::GFx::Value arrIdxVal;
            if (!elem.GetMember("arrayIndex", &arrIdxVal)) continue;

            std::uint32_t elemArrayIndex = 0;
            if (arrIdxVal.IsUInt()) elemArrayIndex = arrIdxVal.GetUInt();
            else if (arrIdxVal.IsNumber()) elemArrayIndex = static_cast<std::uint32_t>(arrIdxVal.GetNumber());

            if (elemArrayIndex == marker.arrayIndex) {
                Scaleform::GFx::Value posIdx;
                posIdx = i;
                holder.SetMember("_uiSelectedMarkerIndex", posIdx);

                Scaleform::GFx::Value markerId;
                markerId = marker.arrayIndex;
                holder.SetMember("_uiSelectedMarkerId", markerId);

                m_lastGameSelectedIndex = static_cast<std::int32_t>(marker.arrayIndex);
                break;
            }
        }
    }

    // Call SetCenterMarker to center the map view
    Scaleform::GFx::Value args[1];
    args[0] = marker.arrayIndex;
    Scaleform::GFx::Value result;
    const_cast<Scaleform::GFx::Value&>(mapPageObj).Invoke("SetCenterMarker", &result, args, 1);
}

void PipBoyMapPage::OnSetReferencePoint()
{
    if (m_currentIndex < 0 || m_filteredIndices.empty()) {
        // Reset to player position
        m_useCustomReference = false;
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (player) {
            RE::NiPoint3 pos = player->GetPosition();
            m_referenceX = pos.x;
            m_referenceY = pos.y;
        }
        m_referenceName = "player";
        RecalculateAllDistances();
        SpeakText("Distance from: player");
        return;
    }

    auto& marker = m_markers[m_filteredIndices[m_currentIndex]];

    // Check if this is the player marker (type 82)
    if (marker.type == 82) {
        m_useCustomReference = false;
        m_referenceX = marker.x;
        m_referenceY = marker.y;
        m_referenceName = "player";
        RecalculateAllDistances();
        SpeakText("Distance from: player");
        return;
    }

    // Set this marker as the reference point
    m_useCustomReference = true;
    m_referenceX = marker.x;
    m_referenceY = marker.y;
    m_referenceName = marker.name;

    RecalculateAllDistances();
    SpeakText("Distance from: " + marker.name);
}

void PipBoyMapPage::OnCycleFilter()
{
    // Cycle: All -> Quests -> Settlements -> Discovered -> Undiscovered -> All
    int next = (static_cast<int>(m_filter) + 1) % 5;
    m_filter = static_cast<MarkerFilter>(next);

    // Rebuild filtered indices
    m_filteredIndices.clear();
    for (std::int32_t i = 0; i < static_cast<std::int32_t>(m_markers.size()); ++i) {
        if (MarkerMatchesFilter(m_markers[i])) {
            m_filteredIndices.push_back(i);
        }
    }

    // Sort filtered indices by distance
    std::sort(m_filteredIndices.begin(), m_filteredIndices.end(),
        [this](std::int32_t a, std::int32_t b) {
            return m_markers[a].distance < m_markers[b].distance;
        });

    m_currentIndex = m_filteredIndices.empty() ? -1 : 0;
    AnnounceFilterChange();
}

void PipBoyMapPage::BuildMarkerList()
{
    m_markers.clear();
    m_filteredIndices.clear();
    m_currentIndex = -1;

    auto ui = RE::UI::GetSingleton();
    if (!ui) return;

    auto menu = ui->GetMenu("PipboyMenu"sv);
    if (!menu || !menu->uiMovie) return;

    // Access dataObj through the PipboyMenu's C++ member (same pattern as PipBoyPageBase)
    auto* pipboyMenu = static_cast<RE::PipboyMenu*>(menu.get());
    const auto& dataObj = pipboyMenu->dataObj;

    if (dataObj.IsUndefined() || !dataObj.IsObject()) {
        REX::WARN("PipBoyMapPage: dataObj is not valid");
        return;
    }

    // Get the marker array from dataObj
    const char* memberName = m_isLocalMap ? "LocalMapMarkers" : "WorldMapMarkers";

    Scaleform::GFx::Value markerArray;
    if (!dataObj.GetMember(memberName, &markerArray) || !markerArray.IsArray()) {
        REX::WARN("PipBoyMapPage: Failed to get {} from dataObj", memberName);
        return;
    }

    std::uint32_t arraySize = markerArray.GetArraySize();
    REX::INFO("PipBoyMapPage: Found {} markers in {}", arraySize, m_isLocalMap ? "Local Map" : "World Map");

    for (std::uint32_t i = 0; i < arraySize; ++i) {
        Scaleform::GFx::Value markerObj;
        if (!markerArray.GetElement(i, &markerObj) || !markerObj.IsObject()) {
            continue;
        }

        MapMarker marker;

        // Get marker properties
        Scaleform::GFx::Value val;

        // name (string)
        if (markerObj.GetMember("name", &val) && val.IsString()) {
            marker.name = val.GetString();
        }

        // type (uint)
        if (markerObj.GetMember("type", &val)) {
            if (val.IsUInt()) marker.type = val.GetUInt();
            else if (val.IsInt()) marker.type = static_cast<std::uint32_t>(val.GetInt());
            else if (val.IsNumber()) marker.type = static_cast<std::uint32_t>(val.GetNumber());
        }

        // arrayIndex (uint)
        if (markerObj.GetMember("arrayIndex", &val)) {
            if (val.IsUInt()) marker.arrayIndex = val.GetUInt();
            else if (val.IsInt()) marker.arrayIndex = static_cast<std::uint32_t>(val.GetInt());
            else if (val.IsNumber()) marker.arrayIndex = static_cast<std::uint32_t>(val.GetNumber());
        }

        // x, y (number) - world coordinates
        if (markerObj.GetMember("x", &val)) {
            if (val.IsNumber()) marker.x = static_cast<float>(val.GetNumber());
            else if (val.IsInt()) marker.x = static_cast<float>(val.GetInt());
        }
        if (markerObj.GetMember("y", &val)) {
            if (val.IsNumber()) marker.y = static_cast<float>(val.GetNumber());
            else if (val.IsInt()) marker.y = static_cast<float>(val.GetInt());
        }

        // undiscovered (bool)
        if (markerObj.GetMember("undiscovered", &val) && val.IsBoolean()) {
            marker.undiscovered = val.GetBoolean();
        }

        // dungeonCleared (bool)
        if (markerObj.GetMember("dungeonCleared", &val) && val.IsBoolean()) {
            marker.cleared = val.GetBoolean();
        }

        // workshopOwned (bool)
        if (markerObj.GetMember("workshopOwned", &val) && val.IsBoolean()) {
            marker.isSettlement = val.GetBoolean();
        }

        // workshopPopulation (uint)
        if (markerObj.GetMember("workshopPopulation", &val)) {
            if (val.IsUInt()) marker.population = val.GetUInt();
            else if (val.IsInt()) marker.population = static_cast<std::uint32_t>(val.GetInt());
            else if (val.IsNumber()) marker.population = static_cast<std::uint32_t>(val.GetNumber());
        }

        // workshopHappyPct (uint)
        if (markerObj.GetMember("workshopHappyPct", &val)) {
            if (val.IsUInt()) marker.happiness = val.GetUInt();
            else if (val.IsInt()) marker.happiness = static_cast<std::uint32_t>(val.GetInt());
            else if (val.IsNumber()) marker.happiness = static_cast<std::uint32_t>(val.GetNumber());
        }

        // Settlement type check (type 13)
        if (marker.type == 13) {
            marker.isSettlement = true;
        }

        // Skip markers with no name (except player location)
        if (marker.name.empty() && marker.type != 82) {
            continue;
        }

        // Calculate distance and direction from reference point
        CalculateDistanceAndDirection(marker);

        m_markers.push_back(marker);
    }

    REX::INFO("PipBoyMapPage: Loaded {} valid markers", m_markers.size());

    // Build filtered indices
    for (std::int32_t i = 0; i < static_cast<std::int32_t>(m_markers.size()); ++i) {
        if (MarkerMatchesFilter(m_markers[i])) {
            m_filteredIndices.push_back(i);
        }
    }

    // Sort by distance
    std::sort(m_filteredIndices.begin(), m_filteredIndices.end(),
        [this](std::int32_t a, std::int32_t b) {
            return m_markers[a].distance < m_markers[b].distance;
        });

    REX::INFO("PipBoyMapPage: {} markers match current filter ({})",
        m_filteredIndices.size(), GetFilterName(m_filter));
}

void PipBoyMapPage::SelectMarkerAtIndex(std::int32_t a_index)
{
    if (a_index < 0 || a_index >= static_cast<std::int32_t>(m_filteredIndices.size())) {
        return;
    }

    // Just announce the marker - centering is done on Home key press
    AnnounceCurrentMarker();
}

void PipBoyMapPage::AnnounceCurrentMarker(bool a_fullDetails, bool a_interrupt)
{
    if (m_currentIndex < 0 || m_filteredIndices.empty()) {
        return;
    }

    auto& marker = m_markers[m_filteredIndices[m_currentIndex]];

    std::string announcement;

    // Name
    if (marker.name.empty()) {
        if (marker.type == 82) {
            announcement = "Player Location";
        } else {
            announcement = "Unknown";
        }
    } else {
        announcement = CleanLocalizationString(marker.name);
    }

    // Type
    announcement += ", " + GetMarkerTypeName(marker.type);

    // Distance and direction
    if (marker.distance >= 0) {
        int distUnits = static_cast<int>(marker.distance);
        announcement += ", " + std::to_string(distUnits) + " units " + marker.direction;
    }

    // Full details mode (Home key)
    if (a_fullDetails) {
        // Undiscovered status
        if (marker.undiscovered) {
            announcement += ", undiscovered";
        }

        // Cleared status
        if (marker.cleared) {
            announcement += ", cleared";
        }

        // Settlement info
        if (marker.isSettlement && marker.population > 0) {
            announcement += ", population " + std::to_string(marker.population);
            announcement += ", " + std::to_string(marker.happiness) + " percent happiness";
        }

        // Distance reference info
        if (m_useCustomReference) {
            announcement += ". Distance from " + m_referenceName;
        }
    }

    SpeakText(announcement, a_interrupt);
}

std::string PipBoyMapPage::GetMarkerTypeName(std::uint32_t a_type)
{
    // Map marker types based on RE::MARKER_TYPE enum
    switch (a_type) {
        case 0: return "Cave";
        case 1: return "City";
        case 2: return "Diamond City";
        case 3: return "Encampment";
        case 4: return "Factory";
        case 5: return "Government Building";
        case 6: return "Metro";
        case 7: return "Military Base";
        case 8: return "Landmark";
        case 9: return "Office Building";
        case 10: return "Ruins";
        case 11: return "Urban Ruins";
        case 12: return "Sanctuary";
        case 13: return "Settlement";
        case 14: return "Sewer";
        case 15: return "Vault";
        case 16: return "Airfield";
        case 17: return "Bunker Hill";
        case 18: return "Camper";
        case 19: return "Car";
        case 20: return "Church";
        case 21: return "Country Club";
        case 22: return "Custom House";
        case 23: return "Drive-In";
        case 24: return "Highway";
        case 25: return "Faneuil Hall";
        case 26: return "Farm";
        case 27: return "Gas Station";
        case 28: return "Forest";
        case 29: return "Goodneighbor";
        case 30: return "Graveyard";
        case 31: return "Hospital";
        case 32: return "Industrial Dome";
        case 33: return "Industrial Stacks";
        case 34: return "Institute";
        case 35: return "Irish Pride";
        case 36: return "Junkyard";
        case 37: return "Observatory";
        case 38: return "Pier";
        case 39: return "Pond";
        case 40: return "Quarry";
        case 41: return "Radioactive Area";
        case 42: return "Radio Tower";
        case 43: return "Salem";
        case 44: return "School";
        case 45: return "Shipwreck";
        case 46: return "Submarine";
        case 47: return "Swan Pond";
        case 48: return "Town";
        case 49: return "Brotherhood of Steel";
        case 50: return "Brownstone";
        case 51: return "Bunker";
        case 52: return "Castle";
        case 53: return "Skyscraper";
        case 54: return "Libertalia";
        case 55: return "Low-Rise";
        case 56: return "Minutemen";
        case 57: return "Police Station";
        case 58: return "Prydwen";
        case 59: return "Railroad";
        case 60: return "Railroad Station";
        case 61: return "Satellite";
        case 62: return "Sentinel";
        case 63: return "USS Constitution";
        case 64: return "Mechanist";
        case 65: return "Raider Settlement";
        case 66: return "Vassal Settlement";
        case 67: return "Potential Settlement";
        case 68: return "Bottling Plant";
        case 69: return "Galactic Zone";
        case 70: return "Hub";
        case 71: return "Kiddie Kingdom";
        case 72: return "Monorail";
        case 73: return "Rides";
        case 74: return "Safari";
        case 75: return "Wild West";
        case 76: return "Point of Interest";
        case 77: return "Door";
        case 78: return "Quest";
        case 79: return "Quest Door";
        case 80: return "Quest";
        case 81: return "Custom Marker";
        case 82: return "Player Location";
        case 83: return "Power Armor";
        case 84: return "Dogmeat";
        case 85: return "Companion";
        default: return "Location";
    }
}

std::string PipBoyMapPage::GetDirectionString(float a_dx, float a_dy)
{
    // Calculate angle in degrees (0 = North, 90 = East, etc.)
    float angle = std::atan2(a_dx, a_dy) * 180.0f / 3.14159265f;

    // Normalize to 0-360
    if (angle < 0) {
        angle += 360.0f;
    }

    // Convert to 8 cardinal directions
    // N = 337.5-22.5, NE = 22.5-67.5, E = 67.5-112.5, etc.
    if (angle >= 337.5f || angle < 22.5f) return "north";
    if (angle >= 22.5f && angle < 67.5f) return "northeast";
    if (angle >= 67.5f && angle < 112.5f) return "east";
    if (angle >= 112.5f && angle < 157.5f) return "southeast";
    if (angle >= 157.5f && angle < 202.5f) return "south";
    if (angle >= 202.5f && angle < 247.5f) return "southwest";
    if (angle >= 247.5f && angle < 292.5f) return "west";
    if (angle >= 292.5f && angle < 337.5f) return "northwest";

    return "";
}

bool PipBoyMapPage::MarkerMatchesFilter(const MapMarker& a_marker)
{
    switch (m_filter) {
        case MarkerFilter::All:
            return true;

        case MarkerFilter::Quests:
            // Quest markers are types 78, 79, 80
            return a_marker.type >= 78 && a_marker.type <= 80;

        case MarkerFilter::Settlements:
            // Settlement type (13) or workshop-owned locations
            return a_marker.type == 13 || a_marker.isSettlement;

        case MarkerFilter::Discovered:
            return !a_marker.undiscovered;

        case MarkerFilter::Undiscovered:
            return a_marker.undiscovered;
    }

    return true;
}

void PipBoyMapPage::AnnounceFilterChange()
{
    std::string announcement = "Filter: ";
    announcement += GetFilterName(m_filter);
    announcement += ", " + std::to_string(m_filteredIndices.size()) + " markers";

    SpeakText(announcement);

    // Announce first marker if any
    if (!m_filteredIndices.empty() && m_currentIndex >= 0) {
        AnnounceCurrentMarker();
    }
}

const char* PipBoyMapPage::GetFilterName(MarkerFilter a_filter)
{
    switch (a_filter) {
        case MarkerFilter::All: return "All";
        case MarkerFilter::Quests: return "Quests";
        case MarkerFilter::Settlements: return "Settlements";
        case MarkerFilter::Discovered: return "Discovered";
        case MarkerFilter::Undiscovered: return "Undiscovered";
        default: return "Unknown";
    }
}

std::string PipBoyMapPage::GetLocalizedTabName(std::int32_t a_tabIndex)
{
    static const char* kTabKeys[] = { "$World Map", "$Local Map" };
    if (a_tabIndex >= 0 && a_tabIndex < 2) {
        return LocalizationHelper::Translate(kTabKeys[a_tabIndex]);
    }
    return "Unknown";
}

void PipBoyMapPage::CalculateDistanceAndDirection(MapMarker& a_marker)
{
    // Use reference point (player position or custom marker)
    float refX = m_useCustomReference ? m_referenceX : m_playerX;
    float refY = m_useCustomReference ? m_referenceY : m_playerY;

    float dx = a_marker.x - refX;
    float dy = a_marker.y - refY;

    a_marker.distance = std::sqrt(dx * dx + dy * dy);
    a_marker.direction = GetDirectionString(dx, dy);
}

void PipBoyMapPage::RecalculateAllDistances()
{
    // Recalculate distances for all markers
    for (auto& marker : m_markers) {
        CalculateDistanceAndDirection(marker);
    }

    // Re-sort the filtered list
    std::sort(m_filteredIndices.begin(), m_filteredIndices.end(),
        [this](std::int32_t a, std::int32_t b) {
            return m_markers[a].distance < m_markers[b].distance;
        });

    // Reset to first marker after re-sort
    if (!m_filteredIndices.empty()) {
        m_currentIndex = 0;
    }
}
