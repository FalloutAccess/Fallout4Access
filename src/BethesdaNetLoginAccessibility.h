#pragma once

#include "pch.h"
#include <RE/M/MainMenu.h>

// Screen reader accessibility for the Bethesda.net sign-in flow.
//
// The login UI is Main_LoginHelper.swf, loaded by MainMenu as the content of
// the public Loader `root.LoginLoader_mc` and added under `root.LoginHolder_mc`.
// It is NOT a separate RE::IMenu; it lives inside the MainMenu movie and is
// active when MainMenu's currentState == "LoginState".
//
// Integration: driven per-frame from MainMenuHook::CheckStateChanges while
// ActivePanel == ActivePanel::BethesdaNetLogin. Observation only; no input is
// injected. The password field is never read back as text.
class BethesdaNetLoginAccessibility
{
public:
	static BethesdaNetLoginAccessibility* GetSingleton();

	// Called once when the login state becomes active. Clears cached state.
	void OnOpened();

	// Called every frame while the login state is active.
	void CheckState(RE::MainMenu* a_menu);

	// Called when the host menu closes. Clears cached state.
	void Reset();

	bool IsOpen() const { return m_isOpen; }

private:
	BethesdaNetLoginAccessibility() = default;
	~BethesdaNetLoginAccessibility() = default;
	BethesdaNetLoginAccessibility(const BethesdaNetLoginAccessibility&) = delete;
	BethesdaNetLoginAccessibility(BethesdaNetLoginAccessibility&&) = delete;
	BethesdaNetLoginAccessibility& operator=(const BethesdaNetLoginAccessibility&) = delete;
	BethesdaNetLoginAccessibility& operator=(BethesdaNetLoginAccessibility&&) = delete;

	// The mutually-exclusive pages of Main_LoginHelper, in detection priority
	// order (Spinner overlays everything; NewAccount is the default screen).
	enum class Page
	{
		None,
		Spinner,
		EULA,
		Login,
		NewAccount
	};

	// Resolve the Main_LoginHelper instance (loader content) with a primary
	// path and a display-list fallback. Logs the first failure once.
	bool GetLoginRoot(RE::MainMenu* a_menu, Scaleform::GFx::Value& a_out);

	// Determine the active page from the page clips' `.visible` flags.
	Page DetectPage(Scaleform::GFx::Value& a_root);

	// Build the spoken announcement for entering a_page (header + field summary).
	std::string BuildPageAnnouncement(Scaleform::GFx::Value& a_root, Page a_page);

	// Read the transient error string for the current page ("" if none).
	std::string GetError(Scaleform::GFx::Value& a_root, Page a_page);

	// Read the spinner's status text ("" if none).
	std::string GetSpinnerText(Scaleform::GFx::Value& a_root);

	void SpeakText(const std::string& a_text);

	bool        m_isOpen = false;
	Page        m_lastPage = Page::None;
	std::string m_lastError;
	std::string m_lastSpinnerText;
	std::string m_lastSpokenText;

	// Scaleform paths to the loader content (resolved from the MainMenu root).
	static constexpr const char* LOGIN_CONTENT_PATH = "root.LoginLoader_mc.content";
	static constexpr const char* LOGIN_HOLDER_PATH = "root.LoginHolder_mc";
};
