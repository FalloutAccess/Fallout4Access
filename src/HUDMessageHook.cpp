#include "HUDMessageHook.h"
#include "InstructionLength.h"
#include <SRAL.h>

HUDMessageHook* HUDMessageHook::GetSingleton()
{
    static HUDMessageHook singleton;
    return &singleton;
}

void HUDMessageHook::Install()
{
    // Installation guard - prevent double installation
    if (m_installed) {
        REX::DEBUG("HUDMessageHook: Already installed");
        return;
    }

    // Get the address of ShowHUDMessage using its ID
    REL::Relocation<std::uintptr_t> target{ kShowHUDMessageID };
    auto targetAddr = target.address();

    REX::INFO("HUDMessageHook: Target function at {:X}", targetAddr);

    // We need to create a proper trampoline that:
    // 1. Executes the original function's first instructions
    // 2. Jumps back to the rest of the original function
    //
    // Use Zydis to automatically detect instruction boundaries.
    // We need at least sizeof(JMP14) = 14 bytes to write our jump.
    constexpr std::size_t kMinBytesNeeded = sizeof(REL::ASM::JMP14);  // 14 bytes

    std::size_t bytesToCopy = InstructionLength::CalculateMinimumCopySize(targetAddr, kMinBytesNeeded);
    if (bytesToCopy == 0) {
        REX::ERROR("HUDMessageHook: Failed to calculate instruction boundary, hook not installed");
        return;
    }

    REX::INFO("HUDMessageHook: Detected instruction boundary at {} bytes", bytesToCopy);

    std::size_t trampolineSize = bytesToCopy + sizeof(REL::ASM::JMP14);

    // Allocate trampoline memory
    auto& trampoline = REL::GetTrampoline();
    trampoline.create(trampolineSize + 64);  // Extra for the branch

    auto* trampolineMem = static_cast<std::byte*>(trampoline.allocate(trampolineSize));
    if (!trampolineMem) {
        REX::ERROR("HUDMessageHook: Failed to allocate trampoline memory, hook not installed");
        // Don't install the hook - we can't call the original function safely
        return;
    }

    // Copy original bytes to trampoline
    std::memcpy(trampolineMem, reinterpret_cast<void*>(targetAddr), bytesToCopy);

    // Add JMP14 back to original + bytesToCopy
    auto* jmp = reinterpret_cast<REL::ASM::JMP14*>(trampolineMem + bytesToCopy);
    new (jmp) REL::ASM::JMP14(targetAddr + bytesToCopy);

    // Store the trampoline as our "original" function
    s_originalFunc = reinterpret_cast<OriginalFunc_t>(trampolineMem);

    REX::INFO("HUDMessageHook: Trampoline at {:X}, jumping back to {:X}",
        reinterpret_cast<std::uintptr_t>(trampolineMem),
        targetAddr + bytesToCopy);

    // Now overwrite the original function entry with a jump to our hook
    // We need a JMP14 (14 bytes) to reach our hook function
    REL::ASM::JMP14 hookJmp(reinterpret_cast<std::uintptr_t>(&HookedShowHUDMessage));

    // Write the jump to the original function
    REL::WriteSafeData(targetAddr, hookJmp);

    m_installed = true;
    REX::INFO("HUDMessageHook: Installed successfully at {:X}", targetAddr);
}

void HUDMessageHook::HookedShowHUDMessage(const char* a_message, const char* a_sound,
                                           bool a_throttle, bool a_warning)
{
    // Capture the message for screen reader
    if (a_message && a_message[0] != '\0') {
        auto* hook = GetSingleton();
        std::string messageText(a_message);

        // Log all HUD messages for debugging
        REX::INFO("HUD Message: \"{}\" (sound={}, throttle={}, warning={})",
            messageText,
            a_sound ? a_sound : "none",
            a_throttle,
            a_warning);

        // Announce via screen reader if not a duplicate
        if (hook->ShouldAnnounce(messageText)) {
            hook->RecordAnnouncement(messageText);
            hook->SpeakText(messageText);
        }
    }

    // Call the original function via our trampoline
    if (s_originalFunc) {
        s_originalFunc(a_message, a_sound, a_throttle, a_warning);
    }
}

std::uint64_t HUDMessageHook::GetCurrentTimeMs() const
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );
}

bool HUDMessageHook::ShouldAnnounce(const std::string& a_text)
{
    if (a_text.empty()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_announcementHistory.find(a_text);
    if (it == m_announcementHistory.end()) {
        return true;
    }

    auto now = GetCurrentTimeMs();
    return (now - it->second) >= kDeduplicationWindowMs;
}

void HUDMessageHook::RecordAnnouncement(const std::string& a_text)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    m_announcementHistory[a_text] = GetCurrentTimeMs();

    // Clean old entries periodically to prevent memory growth
    static std::uint64_t lastCleanTime = 0;
    auto now = GetCurrentTimeMs();

    if (now - lastCleanTime >= 10000) {  // Clean every 10 seconds
        lastCleanTime = now;
        for (auto it = m_announcementHistory.begin(); it != m_announcementHistory.end();) {
            if ((now - it->second) > kDeduplicationWindowMs * 2) {
                it = m_announcementHistory.erase(it);
            } else {
                ++it;
            }
        }
    }
}

void HUDMessageHook::SpeakText(const std::string& a_text)
{
    if (a_text.empty()) return;

    if (SRAL_GetCurrentEngine() != 0) {
        SRAL_Speak(a_text.c_str(), false);  // Never interrupt per CLAUDE.md
    }
}
