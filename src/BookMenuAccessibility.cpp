#include "BookMenuAccessibility.h"
#include "SpeechManager.h"

BookMenuAccessibility* BookMenuAccessibility::GetSingleton()
{
    static BookMenuAccessibility singleton;
    return &singleton;
}

void BookMenuAccessibility::Install()
{
    if (m_installed) {
        REX::DEBUG("BookMenuAccessibility: Already installed");
        return;
    }

    // Hook BookMenu::AdvanceMovie (vtable index 4).
    // BookMenu has no C++ class in CommonLibF4, so we use RE::IMenu* as the this type.
    static REL::THookVFT<void(RE::IMenu*, float, std::uint64_t)> hook(
        VTABLE::BookMenu[0],
        4,  // AdvanceMovie is at index 4 (consistent across all IMenu subclasses)
        HookedAdvanceMovie
    );

    if (hook.Enable()) {
        s_advanceMovieHook = &hook;
        m_installed = true;
        REX::INFO("BookMenuAccessibility: AdvanceMovie hook installed successfully");
    } else {
        REX::ERROR("BookMenuAccessibility: Failed to install AdvanceMovie hook");
    }
}

void BookMenuAccessibility::OnBookMenuOpened()
{
    m_menuOpen = true;
    m_announced = false;
    m_waitFrames = 0;
    m_lastPageText.clear();
    REX::INFO("BookMenuAccessibility: menu opened");
}

void BookMenuAccessibility::OnBookMenuClosed()
{
    m_menuOpen = false;
    m_announced = false;
    m_lastPageText.clear();
    REX::INFO("BookMenuAccessibility: menu closed");
}

void BookMenuAccessibility::HookedAdvanceMovie(RE::IMenu* a_this, float a_timeDelta, std::uint64_t a_time)
{
    if (s_advanceMovieHook) {
        (*s_advanceMovieHook)(a_this, a_timeDelta, a_time);
    }

    auto* self = GetSingleton();
    if (self->m_menuOpen) {
        self->OnAdvanceMovie(a_this);
    }
}

void BookMenuAccessibility::OnAdvanceMovie(RE::IMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) return;

    std::string pageText = ReadVisiblePageText(a_menu);

    if (!m_announced) {
        m_waitFrames++;

        if (pageText.empty()) {
            if (m_waitFrames >= MAX_WAIT_FRAMES) {
                SpeakText(IsNote(a_menu) ? "Note" : "Book");
                m_announced = true;
            }
            return;
        }

        m_lastPageText = pageText;
        SpeakText((IsNote(a_menu) ? "Note: " : "Book: ") + pageText);
        m_announced = true;
        return;
    }

    // Already announced — detect page changes
    if (!pageText.empty() && pageText != m_lastPageText) {
        m_lastPageText = pageText;
        SpeakText(pageText);
    }
}

std::string BookMenuAccessibility::ReadVisiblePageText(RE::IMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) return "";

    Scaleform::GFx::Value root;
    if (!a_menu->uiMovie->GetVariable(&root, "root") || !root.IsObject()) return "";

    Scaleform::GFx::Value numChildrenVal;
    if (!root.GetMember("numChildren", &numChildrenVal)) return "";

    int numChildren = 0;
    if (numChildrenVal.IsInt())
        numChildren = numChildrenVal.GetInt();
    else if (numChildrenVal.IsNumber())
        numChildren = static_cast<int>(numChildrenVal.GetNumber());
    else
        return "";

    for (int i = 0; i < numChildren; i++) {
        Scaleform::GFx::Value idxArg(static_cast<double>(i));
        Scaleform::GFx::Value child;
        if (!root.Invoke("getChildAt", &child, &idxArg, 1) || !child.IsObject()) continue;

        // Skip invisible children
        Scaleform::GFx::Value visVal;
        if (!child.GetMember("visible", &visVal) || !visVal.IsBoolean() || !visVal.GetBoolean()) continue;

        // Skip the hidden reference template by name
        Scaleform::GFx::Value nameVal;
        if (child.GetMember("name", &nameVal) && nameVal.IsString()) {
            if (std::string_view(nameVal.GetString()) == "ReferenceTextInstance"sv) continue;
        }

        // Presence of PageTextField identifies this as a page instance
        Scaleform::GFx::Value pageField;
        if (!child.GetMember("PageTextField", &pageField) || !pageField.IsObject()) continue;

        Scaleform::GFx::Value textVal;
        if (pageField.GetMember("text", &textVal) && textVal.IsString()) {
            std::string text = TrimText(textVal.GetString());
            if (!text.empty()) return text;
        }
    }

    return "";
}

bool BookMenuAccessibility::IsNote(RE::IMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) return false;

    Scaleform::GFx::Value val;
    if (a_menu->uiMovie->GetVariable(&val, IS_NOTE_PATH) && val.IsBoolean()) {
        return val.GetBoolean();
    }
    return false;
}

std::string BookMenuAccessibility::TrimText(const std::string& a_text)
{
    auto start = a_text.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = a_text.find_last_not_of(" \t\r\n");
    return a_text.substr(start, end - start + 1);
}

void BookMenuAccessibility::SpeakText(const std::string& a_text)
{
    SpeechManager::GetSingleton()->Speak(a_text);
}
