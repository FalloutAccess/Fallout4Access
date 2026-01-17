#include "HUDMessageHook.h"
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
    // For ShowHUDMessage, we'll copy enough bytes and add a jump back.
    // A JMP14 (14-byte absolute jump) needs 14 bytes.
    // We need to copy at least 14 bytes of complete instructions.
    //
    // Typical x64 prologue:
    //   push rbp           (1 byte)  - optional
    //   mov rbp, rsp       (3 bytes) - optional
    //   sub rsp, N         (4-7 bytes)
    //   mov [rsp+X], reg   (4-8 bytes per save)
    //
    // Based on disassembly, the instruction boundary is at 14 bytes:
    //   48 83 EC 58       - sub rsp, 0x58        (4 bytes)
    //   C6 44 24 28 00    - mov [rsp+0x28], 0    (5 bytes) = 9 total
    //   44 88 4C 24 20    - mov [rsp+0x20], r9b  (5 bytes) = 14 total
    // This matches JMP14 size perfectly.

    constexpr std::size_t kBytesToCopy = 14;
    constexpr std::size_t kTrampolineSize = kBytesToCopy + sizeof(REL::ASM::JMP14);

    // Allocate trampoline memory
    auto& trampoline = REL::GetTrampoline();
    trampoline.create(kTrampolineSize + 64);  // Extra for the branch

    auto* trampolineMem = static_cast<std::byte*>(trampoline.allocate(kTrampolineSize));
    if (!trampolineMem) {
        REX::ERROR("HUDMessageHook: Failed to allocate trampoline memory, hook not installed");
        // Don't install the hook - we can't call the original function safely
        return;
    }

    // Copy original bytes to trampoline
    std::memcpy(trampolineMem, reinterpret_cast<void*>(targetAddr), kBytesToCopy);

    // Add JMP14 back to original + kBytesToCopy
    auto* jmp = reinterpret_cast<REL::ASM::JMP14*>(trampolineMem + kBytesToCopy);
    new (jmp) REL::ASM::JMP14(targetAddr + kBytesToCopy);

    // Store the trampoline as our "original" function
    s_originalFunc = reinterpret_cast<OriginalFunc_t>(trampolineMem);

    REX::INFO("HUDMessageHook: Trampoline at {:X}, jumping back to {:X}",
        reinterpret_cast<std::uintptr_t>(trampolineMem),
        targetAddr + kBytesToCopy);

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
