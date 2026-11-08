#pragma once

#include <wx/wx.h>
#include <wx/settings.h>
#ifdef __WXMSW__
#include <wx/msw/registry.h>
#endif

// UI chrome palette that follows the macOS system appearance (dark/light).
// The terminal drawing surface uses TerminalTheme instead; this palette only
// covers wxWidgets panels, lists and dialogs.
struct UiPalette {
    wxColour bgWindow;
    wxColour bgContent;
    wxColour bgRowEven;
    wxColour bgRowOdd;
    wxColour bgHeader;
    wxColour bgSearch;
    wxColour border;
    wxColour textPrimary;
    wxColour textSecondary;
    wxColour textFaint;

    // Custom title bar and tab strip
    wxColour titleBg;
    wxColour tabIdle;
    wxColour tabHover;
    wxColour tabActive;
    wxColour tabBorder;
    wxColour accent;
};

// Temporary crash/stage diagnostics.
#include <fstream>
#include <filesystem>
inline void UiThemeDebugStage(const char* stage) {
    std::ofstream f((std::filesystem::temp_directory_path() / "oterm_theme_debug.log"),
                    std::ios::app);
    if (f.is_open()) f << stage << std::endl;
}

// Whether the OS is currently using a dark appearance for apps.
// On Windows the Personalize registry value is authoritative and works
// regardless of whether wxMSW dark mode could be enabled; elsewhere we use
// wxSystemSettings::GetAppearance().IsDark().
inline bool SystemUsesDarkTheme() {
#ifdef __WXMSW__
    wxRegKey key(wxRegKey::HKCU,
                 "Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize");
    if (key.Open(wxRegKey::Read)) {
        long value = 1;
        if (key.QueryValue("AppsUseLightTheme", &value)) {
            return value == 0;
        }
    }
#endif
    return wxSystemSettings::GetAppearance().IsDark();
}

// Force every descendant to repaint. SetBackgroundColour/SetForegroundColour
// on native wxMSW controls (tree, edit, buttons) only affects the next paint
// cycle — without an explicit Refresh the stale pixels linger and a dialog
// ends up half old-theme / half new-theme.
inline void RepaintUiTree(wxWindow* root) {
    if (!root) return;
    root->Refresh();
    const wxWindowList children = root->GetChildren();
    for (wxWindow* c : children) RepaintUiTree(c);
}

#ifdef __WXMSW__
// Apply the system dark appearance to a top-level window's native title bar
// via DWM. Attribute 20 is the documented one on Win10 20H1+ / Win11; 19 is
// the legacy build's numbering.
inline void ApplyDarkTitleBar(wxWindow* win) {
    if (!win) return;
    HWND hwnd = static_cast<HWND>(win->GetHWND());
    if (!hwnd) return;
    typedef HRESULT (WINAPI *DwmSetWindowAttribute_t)(HWND, DWORD, LPCVOID, DWORD);
    HMODULE dwmapi = LoadLibraryW(L"dwmapi.dll");
    if (!dwmapi) return;
    auto fn = reinterpret_cast<DwmSetWindowAttribute_t>(
        GetProcAddress(dwmapi, "DwmSetWindowAttribute"));
    if (!fn) return;
    const BOOL dark = SystemUsesDarkTheme() ? TRUE : FALSE;
    for (DWORD attr : {20, 19}) {
        if (SUCCEEDED(fn(hwnd, attr, &dark, sizeof(dark)))) break;
    }
}
#else
inline void ApplyDarkTitleBar(wxWindow*) {}
#endif

inline UiPalette CurrentUiPalette() {
    if (SystemUsesDarkTheme()) {
        return {
            wxColour(14, 14, 17),   // bgWindow
            wxColour(20, 20, 24),   // bgContent
            wxColour(23, 23, 28),   // bgRowEven
            wxColour(28, 29, 35),   // bgRowOdd
            wxColour(36, 37, 44),   // bgHeader
            wxColour(30, 31, 37),   // bgSearch
            wxColour(48, 49, 58),   // border
            wxColour(233, 234, 240),// textPrimary
            wxColour(158, 160, 170),// textSecondary
            wxColour(104, 106, 116),// textFaint
            wxColour(45, 45, 45),    // titleBg
            wxColour(30, 30, 30),    // tabIdle
            wxColour(60, 60, 60),    // tabHover
            wxColour(45, 45, 45),    // tabActive
            wxColour(20, 20, 20),    // tabBorder
            wxColour(90, 140, 230),  // accent
        };
    }
    return {
        wxColour(240, 241, 245),   // bgWindow
        wxColour(255, 255, 255),   // bgContent
        wxColour(247, 248, 251),   // bgRowEven
        wxColour(238, 240, 245),   // bgRowOdd
        wxColour(227, 229, 235),   // bgHeader
        wxColour(233, 235, 240),   // bgSearch
        wxColour(196, 199, 208),   // border
        wxColour(28, 29, 34),      // textPrimary
        wxColour(96, 99, 110),     // textSecondary
        wxColour(140, 143, 152),   // textFaint
        wxColour(233, 235, 241),   // titleBg
        wxColour(223, 225, 231),   // tabIdle
        wxColour(207, 210, 218),   // tabHover
        wxColour(246, 247, 250),   // tabActive
        wxColour(178, 181, 190),   // tabBorder
        wxColour(50, 105, 200),    // accent
    };
}
