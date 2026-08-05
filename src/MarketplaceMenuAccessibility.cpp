#include "MarketplaceMenuAccessibility.h"
#include "SpeechManager.h"
#include <excpt.h>  // SEH: EXCEPTION_EXECUTE_HANDLER for crash-safe memory probing

namespace
{
	// Copy a NUL-terminated C string from a char* stored at a_addr into a_buf,
	// guarded by SEH so a non-pointer / unmapped value can't crash the game.
	// POD-only inside __try (no C++ object unwinding). Returns false on failure.
	bool SafeCopyString(std::uintptr_t a_addr, char* a_buf, std::size_t a_len)
	{
		__try {
			const char* const* pp = reinterpret_cast<const char* const*>(a_addr);
			const char*        p = *pp;
			std::uintptr_t     pv = reinterpret_cast<std::uintptr_t>(p);
			// Reject obvious non-pointers (small ints, kernel-space, etc.).
			if (pv < 0x10000 || pv > 0x00007FFFFFFFFFFFULL) {
				return false;
			}
			std::size_t i = 0;
			for (; i + 1 < a_len && p[i] != '\0'; ++i) {
				a_buf[i] = p[i];
			}
			a_buf[i] = '\0';
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	// Copy a_len raw bytes from a_addr into a_buf under SEH. Returns false on fault.
	bool SafeReadBytes(std::uintptr_t a_addr, unsigned char* a_buf, std::size_t a_len)
	{
		__try {
			const unsigned char* p = reinterpret_cast<const unsigned char*>(a_addr);
			for (std::size_t i = 0; i < a_len; ++i) {
				a_buf[i] = p[i];
			}
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	// POD-only SEH copy of a wide string from a wchar_t* stored at a_addr into
	// a_buf. Returns the count copied, or -1 on fault / invalid pointer.
	int SafeCopyWString(std::uintptr_t a_addr, wchar_t* a_buf, std::size_t a_len)
	{
		__try {
			const wchar_t* const* pp = reinterpret_cast<const wchar_t* const*>(a_addr);
			const wchar_t*        p = *pp;
			std::uintptr_t        pv = reinterpret_cast<std::uintptr_t>(p);
			if (pv < 0x10000 || pv > 0x00007FFFFFFFFFFFULL || (pv & 1) != 0) {
				return -1;
			}
			std::size_t n = 0;
			for (; n + 1 < a_len && p[n] != L'\0'; ++n) {
				a_buf[n] = p[n];
			}
			a_buf[n] = L'\0';
			return static_cast<int>(n);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			return -1;
		}
	}

	// Read an MSVC std::string living AT a_addr. Layout (32 bytes):
	//   union { char _Buf[16]; char* _Ptr; };  size_t _Mysize;  size_t _Myres;
	// _Myres (capacity, +0x18) < 16 => SSO (bytes inline at a_addr); >= 16 => heap
	// (char* at a_addr). This is how the char* sweep missed SSO titles. POD-only
	// under SEH. Returns bytes copied, or -1 on fault / implausible fields.
	int SafeReadStdString(std::uintptr_t a_addr, char* a_buf, std::size_t a_len)
	{
		__try {
			const std::size_t size = *reinterpret_cast<const std::size_t*>(a_addr + 0x10);
			const std::size_t cap = *reinterpret_cast<const std::size_t*>(a_addr + 0x18);
			const char*       src;
			if (cap < 16) {
				if (size > 15) {
					return -1;
				}
				src = reinterpret_cast<const char*>(a_addr);
				if (src[size] != '\0') {  // SSO is always NUL-terminated
					return -1;
				}
			} else {
				if (size > cap) {
					return -1;
				}
				src = *reinterpret_cast<const char* const*>(a_addr);
				const std::uintptr_t pv = reinterpret_cast<std::uintptr_t>(src);
				if (pv < 0x10000 || pv > 0x00007FFFFFFFFFFFULL) {
					return -1;
				}
			}
			if (size == 0 || size > 4000) {
				return -1;
			}
			const std::size_t n = (size < a_len - 1) ? size : a_len - 1;
			for (std::size_t i = 0; i < n; ++i) {
				a_buf[i] = src[i];
			}
			a_buf[n] = '\0';
			return static_cast<int>(n);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			return -1;
		}
	}

	// std::wstring variant: _Buf is wchar_t[8], so _Myres < 8 => SSO. Returns
	// wchars copied (NUL-terminated into a_buf), or -1 on fault.
	int SafeReadStdWString(std::uintptr_t a_addr, wchar_t* a_buf, std::size_t a_len)
	{
		__try {
			const std::size_t size = *reinterpret_cast<const std::size_t*>(a_addr + 0x10);
			const std::size_t cap = *reinterpret_cast<const std::size_t*>(a_addr + 0x18);
			const wchar_t*    src;
			if (cap < 8) {
				if (size > 7) {
					return -1;
				}
				src = reinterpret_cast<const wchar_t*>(a_addr);
				if (src[size] != L'\0') {
					return -1;
				}
			} else {
				if (size > cap) {
					return -1;
				}
				src = *reinterpret_cast<const wchar_t* const*>(a_addr);
				const std::uintptr_t pv = reinterpret_cast<std::uintptr_t>(src);
				if (pv < 0x10000 || pv > 0x00007FFFFFFFFFFFULL || (pv & 1) != 0) {
					return -1;
				}
			}
			if (size == 0 || size > 4000) {
				return -1;
			}
			const std::size_t n = (size < a_len - 1) ? size : a_len - 1;
			for (std::size_t i = 0; i < n; ++i) {
				a_buf[i] = src[i];
			}
			a_buf[n] = L'\0';
			return static_cast<int>(n);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			return -1;
		}
	}

	// Read a UTF-16 string from a wchar_t* stored at a_addr, converting BMP code
	// points to UTF-8. "" on any failure. Used to detect wide-string fields the
	// char* reader can't see.
	std::string ReadWStringPtr(std::uintptr_t a_addr)
	{
		wchar_t   wbuf[128];
		const int n = SafeCopyWString(a_addr, wbuf, 128);
		if (n <= 0) {
			return "";
		}
		std::string out;
		for (int i = 0; i < n; ++i) {
			unsigned int cp = static_cast<unsigned short>(wbuf[i]);
			if (cp < 0x80) {
				out += static_cast<char>(cp);
			} else if (cp < 0x800) {
				out += static_cast<char>(0xC0 | (cp >> 6));
				out += static_cast<char>(0x80 | (cp & 0x3F));
			} else {
				out += static_cast<char>(0xE0 | (cp >> 12));
				out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
				out += static_cast<char>(0x80 | (cp & 0x3F));
			}
		}
		return out;
	}

	// Read a string from a char* stored at a_addr; "" on any failure.
	std::string ReadCStringPtr(std::uintptr_t a_addr)
	{
		char buf[512];
		if (!SafeCopyString(a_addr, buf, sizeof(buf))) {
			return "";
		}
		std::string out(buf);
		for (auto& c : out) {
			if (c == '\n' || c == '\r') {
				c = ' ';
			}
		}
		return out;
	}

	// Read an MSVC std::string AT a_addr (SSO or heap), newlines flattened. "" on
	// failure.
	std::string ReadStdString(std::uintptr_t a_addr)
	{
		char      buf[512];
		const int n = SafeReadStdString(a_addr, buf, sizeof(buf));
		if (n <= 0) {
			return "";
		}
		std::string out(buf, static_cast<std::size_t>(n));
		for (auto& c : out) {
			if (c == '\n' || c == '\r') {
				c = ' ';
			}
		}
		return out;
	}

	// Read an MSVC std::wstring AT a_addr (SSO or heap), BMP -> UTF-8. "" on failure.
	std::string ReadStdWString(std::uintptr_t a_addr)
	{
		wchar_t   wbuf[256];
		const int n = SafeReadStdWString(a_addr, wbuf, 256);
		if (n <= 0) {
			return "";
		}
		std::string out;
		for (int i = 0; i < n; ++i) {
			unsigned int cp = static_cast<unsigned short>(wbuf[i]);
			if (cp < 0x80) {
				out += static_cast<char>(cp);
			} else if (cp < 0x800) {
				out += static_cast<char>(0xC0 | (cp >> 6));
				out += static_cast<char>(0x80 | (cp & 0x3F));
			} else {
				out += static_cast<char>(0xE0 | (cp >> 12));
				out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
				out += static_cast<char>(0x80 | (cp & 0x3F));
			}
		}
		return out;
	}

	// TEMP diagnostic: log every offset in [0x08, a_max] of the object at a_base
	// that reads back as a plausible std::string / std::wstring. Used to locate the
	// title field on the card widget / details page.
	void DumpStdStrings(const char* a_tag, std::uintptr_t a_base, std::uintptr_t a_max)
	{
		if (a_base == 0) {
			return;
		}
		std::string sdump;
		for (std::uintptr_t off = 0x08; off <= a_max; off += 0x08) {
			std::string s = ReadStdString(a_base + off).substr(0, 50);
			if (s.size() >= 2) {
				char hdr[16];
				std::snprintf(hdr, sizeof(hdr), "+0x%llX='", static_cast<unsigned long long>(off));
				sdump += hdr;
				sdump += s;
				sdump += "' ";
			}
		}
		REX::INFO("[mp-{}-str] {}", a_tag, sdump);

		std::string wdump;
		for (std::uintptr_t off = 0x08; off <= a_max; off += 0x08) {
			std::string s = ReadStdWString(a_base + off).substr(0, 50);
			if (s.size() >= 2) {
				char hdr[16];
				std::snprintf(hdr, sizeof(hdr), "+0x%llX='", static_cast<unsigned long long>(off));
				wdump += hdr;
				wdump += s;
				wdump += "' ";
			}
		}
		REX::INFO("[mp-{}-wstr] {}", a_tag, wdump);
	}

	std::int32_t ReadI32(std::uintptr_t a_addr)
	{
		return *reinterpret_cast<const std::int32_t*>(a_addr);
	}

	bool ReadBool(std::uintptr_t a_addr)
	{
		return *reinterpret_cast<const char*>(a_addr) != 0;
	}
}

MarketplaceMenuAccessibility* MarketplaceMenuAccessibility::GetSingleton()
{
	static MarketplaceMenuAccessibility singleton;
	return &singleton;
}

void MarketplaceMenuAccessibility::Install()
{
	if (m_installed) {
		return;
	}

	// Resolve the UI_listEntry vtable so we can validate that a shelf's entries
	// are real Creation cards (not the category/search lists, which are also
	// UI_ScrollingList) before dereferencing their content pointer.
	s_listEntryVtableAddr = REL::ID(kListEntryVtableID).address();

	// Two vtable hooks, both resolved via address-library IDs (no raw offsets):
	//   - UI_detailsPage::Update (slot 3) — focus while the details page is open.
	//   - UI_ScrollingList::setSelectedIndex (slot 23) — browse focus moves.
	static REL::THookVFT<void(void*, float, void*, void*)> detailHook(
		REL::ID(kDetailsPageVtableID), kUpdateSlot, HookedDetailUpdate);
	static REL::THookVFT<void(void*, int, void*, void*)> selectHook(
		REL::ID(kScrollListVtableID), kSetSelectedIndexSlot, HookedSetSelectedIndex);

	const bool detailOk = detailHook.Enable();
	const bool selectOk = selectHook.Enable();
	if (detailOk) {
		s_updateHook = &detailHook;
	}
	if (selectOk) {
		s_selectHook = &selectHook;
	}
	m_installed = detailOk || selectOk;
	REX::INFO("MarketplaceMenuAccessibility: hooks installed (details={}, browse={})", detailOk, selectOk);
}

void MarketplaceMenuAccessibility::HookedDetailUpdate(void* a_this, float a_delta, void* a_p3, void* a_p4)
{
	// Forward all four args exactly so the original isn't corrupted.
	if (s_updateHook) {
		(*s_updateHook)(a_this, a_delta, a_p3, a_p4);
	}
	GetSingleton()->OnDetailUpdate(a_this);
}

void MarketplaceMenuAccessibility::OnDetailUpdate(void* a_detailsPage)
{
	if (!a_detailsPage) {
		return;
	}
	// Focused MarketplaceContent* lives at UI_detailsPage+0x88.
	std::uintptr_t content = *reinterpret_cast<const std::uintptr_t*>(
		reinterpret_cast<std::uintptr_t>(a_detailsPage) + kFocusedContent);
	// TEMP: sweep the details-page object to locate its title + action-button text
	// fields (the details page is a different object than the browse card widget).
	// Browse already announced this item, so we don't speak again here yet.
	if (content != 0 && content != m_lastDetailSwept) {
		m_lastDetailSwept = content;
		DumpStdStrings("detail", reinterpret_cast<std::uintptr_t>(a_detailsPage), 0x300);
	}
}

void MarketplaceMenuAccessibility::HookedSetSelectedIndex(void* a_this, int a_newIndex, void* a_p3, void* a_p4)
{
	// Forward all four args exactly so the original isn't corrupted.
	if (s_selectHook) {
		(*s_selectHook)(a_this, a_newIndex, a_p3, a_p4);
	}
	GetSingleton()->OnShelfSelect(a_this);
}

void MarketplaceMenuAccessibility::OnShelfSelect(void* a_scrollingList)
{
	if (!a_scrollingList) {
		return;
	}
	const std::uintptr_t list = reinterpret_cast<std::uintptr_t>(a_scrollingList);

	const std::int32_t idx = *reinterpret_cast<const std::int32_t*>(list + kSelIndex);
	if (idx < 0) {
		return;
	}

	// entries: [begin, end) of UI_listEntry* (stride 8). +0x98 indexes this
	// array directly (per-shelf entry array is not virtualized).
	const std::uintptr_t begin = *reinterpret_cast<const std::uintptr_t*>(list + kEntriesBegin);
	const std::uintptr_t end = *reinterpret_cast<const std::uintptr_t*>(list + kEntriesEnd);
	if (begin == 0 || end < begin) {
		return;
	}
	const std::size_t count = static_cast<std::size_t>((end - begin) / sizeof(void*));
	if (static_cast<std::size_t>(idx) >= count) {
		return;
	}

	const std::uintptr_t entry = *reinterpret_cast<const std::uintptr_t*>(begin + static_cast<std::size_t>(idx) * sizeof(void*));
	if (entry == 0) {
		return;
	}

	// Gate out non-card lists (category sidebar / search are also UI_ScrollingList
	// but their entries aren't UI_listEntry): only read +0x70 on real cards.
	if (s_listEntryVtableAddr != 0 &&
		*reinterpret_cast<const std::uintptr_t*>(entry) != s_listEntryVtableAddr) {
		return;
	}

	const std::uintptr_t content = *reinterpret_cast<const std::uintptr_t*>(entry + kEntryContent);
	// The localized title is the rendered text cached on the card widget at +0xB0.
	const std::string title = ReadStdString(entry + kEntryTitle);
	AnnounceContent(title, content);
}

void MarketplaceMenuAccessibility::AnnounceContent(const std::string& a_title, std::uintptr_t a_content)
{
	// Need both a name (read off the widget) and the content (for status flags).
	if (a_content == 0 || a_title.empty()) {
		return;
	}
	// Both hooks fire repeatedly — only act when the focused item changes.
	if (a_content == m_lastContent) {
		return;
	}
	m_lastContent = a_content;

	std::string speech = BuildContentSpeech(a_title, a_content);
	if (!speech.empty() && speech != m_lastSpoken) {
		m_lastSpoken = speech;
		SpeakText(speech);
	}
}

std::string MarketplaceMenuAccessibility::BuildContentSpeech(const std::string& a_title, std::uintptr_t a_content)
{
	if (a_title.empty()) {
		return "";
	}

	std::string result = a_title;

	const bool         owned = ReadBool(a_content + kOwned);
	const bool         installed = ReadBool(a_content + kInstalled);
	const bool         downloading = ReadBool(a_content + kDownloading);
	const bool         bundle = ReadBool(a_content + kIsBundle);
	const std::int32_t price = ReadI32(a_content + kPriceCredits);

	// One status, in priority order.
	if (downloading) {
		result += ", downloading";
	} else if (installed) {
		result += ", installed";
	} else if (owned) {
		result += ", owned";
	} else if (price > 0) {
		result += ", " + std::to_string(price) + " credits";
	} else {
		result += ", free";
	}

	if (bundle) {
		result += ", bundle";
	}
	return result;
}

void MarketplaceMenuAccessibility::OnMenuOpen()
{
	Install();
	m_lastContent = 0;
	m_lastSpoken.clear();
	REX::INFO("MarketplaceMenuAccessibility: Creations store opened");
	SpeakText("Creations");
}

void MarketplaceMenuAccessibility::OnMenuClose()
{
	m_lastContent = 0;
	m_lastSpoken.clear();
}

void MarketplaceMenuAccessibility::SpeakText(const std::string& a_text)
{
	if (a_text.empty()) {
		return;
	}
	SpeechManager::GetSingleton()->Speak(a_text);
}
