#pragma once

#include "pch.h"

// Provides accessibility for BookMenu (books, notes, holotapes with text)
// Uses AdvanceMovie vtable hook to detect page changes regardless of input method
// (works for keyboard, gamepad, and remapped controls)
class BookMenuAccessibility
{
public:
    static BookMenuAccessibility* GetSingleton();

    // Install the vtable hook (call once at plugin startup)
    void Install();

    // Called by MenuAccessibility when BookMenu opens/closes
    void OnBookMenuOpened();
    void OnBookMenuClosed();

private:
    BookMenuAccessibility() = default;
    ~BookMenuAccessibility() = default;
    BookMenuAccessibility(const BookMenuAccessibility&) = delete;
    BookMenuAccessibility(BookMenuAccessibility&&) = delete;
    BookMenuAccessibility& operator=(const BookMenuAccessibility&) = delete;
    BookMenuAccessibility& operator=(BookMenuAccessibility&&) = delete;

    // The hooked AdvanceMovie function
    static void HookedAdvanceMovie(RE::IMenu* a_this, float a_timeDelta, std::uint64_t a_time);

    // Called every frame while menu is open
    void OnAdvanceMovie(RE::IMenu* a_menu);

    // Read the text from the currently visible page by scanning root's display children.
    // Skips the hidden ReferenceTextInstance template and returns the first visible
    // child's PageTextField.text content.
    std::string ReadVisiblePageText(RE::IMenu* a_menu);

    // Check whether this is a note (true) vs a physical book (false)
    bool IsNote(RE::IMenu* a_menu);

    // Trim leading/trailing whitespace
    static std::string TrimText(const std::string& a_text);

    void SpeakText(const std::string& a_text);

    // State
    bool m_installed = false;
    bool m_menuOpen = false;
    bool m_announced = false;       // true after initial "Book/Note: text" announcement
    int m_waitFrames = 0;           // frames waited for text to become available
    std::string m_lastPageText;     // last spoken page text, for change detection

    // Max frames to wait for Scaleform to populate text before giving up (magazines have no text)
    static constexpr int MAX_WAIT_FRAMES = 30;

    // Scaleform paths
    static constexpr const char* IS_NOTE_PATH = "root.bNote";

    // The vtable hook (BookMenu has no C++ class in CommonLibF4, so we use RE::IMenu*)
    static inline REL::THookVFT<void(RE::IMenu*, float, std::uint64_t)>* s_advanceMovieHook = nullptr;
};
