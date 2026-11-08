#pragma once

#include <wx/wx.h>
#include <wx/settings.h>

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

inline UiPalette CurrentUiPalette() {
    if (wxSystemSettings::GetAppearance().IsDark()) {
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
