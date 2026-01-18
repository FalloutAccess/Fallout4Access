#include "DeferredItemAnnouncer.h"
#include "SpeechManager.h"
#include <F4SE/F4SE.h>
#include <RE/U/UI.h>
#include <RE/P/PipboyMenu.h>
#include <SRAL.h>

void DeferredItemAnnouncer::ScheduleAnnouncement(const ItemIdentity& a_identity)
{
    if (!a_identity.IsValid()) {
        REX::DEBUG("DeferredItemAnnouncer: Invalid identity, skipping");
        return;
    }

    // Check for duplicate (same item as last announced)
    if (a_identity == m_lastAnnounced) {
        REX::DEBUG("DeferredItemAnnouncer: Duplicate announcement, skipping '{}'", a_identity.name);
        return;
    }

    // Schedule for next frame - this gives InfoObj time to update
    auto* taskInterface = F4SE::GetTaskInterface();
    if (!taskInterface) {
        REX::ERROR("DeferredItemAnnouncer: F4SE task interface unavailable");
        return;
    }

    // Capture identity by value for the lambda
    taskInterface->AddTask([this, expectedIdentity = a_identity]() {
        ExecuteAnnouncement(expectedIdentity);
    });

    REX::DEBUG("DeferredItemAnnouncer: Scheduled announcement for '{}'", a_identity.name);
}

void DeferredItemAnnouncer::Reset()
{
    m_lastAnnounced.Clear();
    REX::DEBUG("DeferredItemAnnouncer: Reset");
}

void DeferredItemAnnouncer::ExecuteAnnouncement(const ItemIdentity& a_expectedIdentity)
{
    // Safety check: Verify menu still exists before proceeding
    // This handles the edge case where the menu closes during the 1-frame delay
    auto ui = RE::UI::GetSingleton();
    if (!ui) {
        REX::DEBUG("DeferredItemAnnouncer: UI unavailable during deferred execution");
        return;
    }

    auto menu = ui->GetMenu("PipboyMenu"sv);
    if (!menu) {
        REX::DEBUG("DeferredItemAnnouncer: PipboyMenu closed during deferred execution");
        return;
    }

    // Check for duplicate (same item as last announced)
    // We trust the identity captured at schedule time since the 1-frame delay is short
    if (a_expectedIdentity == m_lastAnnounced) {
        REX::DEBUG("DeferredItemAnnouncer: Duplicate on execution, skipping '{}'", a_expectedIdentity.name);
        return;
    }

    // Extract fresh stats (InfoObj should be updated by now after the 1-frame delay)
    auto stats = ExtractCurrentStats();

    // Build and speak announcement
    auto announcement = BuildAnnouncement(a_expectedIdentity, stats);
    SpeakText(announcement);

    // Update last announced
    m_lastAnnounced = a_expectedIdentity;
}

std::vector<ItemStatFormatter::FormattedStat> DeferredItemAnnouncer::ExtractCurrentStats()
{
    auto ui = RE::UI::GetSingleton();
    if (!ui) return {};

    auto menu = ui->GetMenu("PipboyMenu"sv);
    if (!menu || !menu->uiMovie) return {};

    auto* pipboyMenu = static_cast<RE::PipboyMenu*>(menu.get());
    const auto& dataObj = pipboyMenu->dataObj;

    if (dataObj.IsUndefined() || !dataObj.IsObject()) {
        return {};
    }

    // Access ItemCard_mc.InfoObj from dataObj
    Scaleform::GFx::Value itemCard;
    if (!dataObj.GetMember("ItemCard_mc", &itemCard) || !itemCard.IsObject()) {
        // Try alternative: get via movie GetVariable
        Scaleform::GFx::Value infoObj;
        if (menu->uiMovie->GetVariable(&infoObj, "root.DataObj.ItemCard_mc.InfoObj") && infoObj.IsArray()) {
            return ItemStatFormatter::ParseInfoObj(infoObj);
        }
        REX::DEBUG("DeferredItemAnnouncer: ItemCard_mc not found");
        return {};
    }

    Scaleform::GFx::Value infoObj;
    if (!itemCard.GetMember("InfoObj", &infoObj) || !infoObj.IsArray()) {
        REX::DEBUG("DeferredItemAnnouncer: InfoObj not found or not array");
        return {};
    }

    return ItemStatFormatter::ParseInfoObj(infoObj);
}

std::string DeferredItemAnnouncer::BuildAnnouncement(
    const ItemIdentity& a_identity,
    const std::vector<ItemStatFormatter::FormattedStat>& a_stats)
{
    // Start with basic identity announcement
    std::string announcement = a_identity.BuildBasicAnnouncement();

    // Add stats
    announcement += ItemStatFormatter::BuildStatAnnouncement(a_stats);

    return announcement;
}

void DeferredItemAnnouncer::SpeakText(const std::string& a_text)
{
    SpeechManager::GetSingleton()->Speak(a_text);
}

std::string DeferredItemAnnouncer::CleanString(const std::string& a_text)
{
    if (a_text.empty()) return "";

    std::string result = a_text;

    if (result[0] == '$') {
        result = result.substr(1);
    }

    for (char& c : result) {
        if (c == '_' || c == '-') c = ' ';
    }

    return result;
}
