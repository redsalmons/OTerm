#include "TerminalTheme.h"

namespace {

// Ocean Dark — deep indigo-grey, Tokyo Night inspired
const TerminalTheme kOceanDark{
    "ocean-dark", "themeOceanDark",
    0x1A1B26, 0xC0CAF5, 0x7AA2F7, 0x9933467C,
    {0x15161E, 0xF7768E, 0x9ECE6A, 0xE0AF68, 0x7AA2F7, 0xBB9AF7, 0x7DCFFF, 0xA9B1D6,
     0x414868, 0xF7768E, 0x9ECE6A, 0xE0AF68, 0x7AA2F7, 0xBB9AF7, 0x7DCFFF, 0xC0CAF5}
};

// Classic Dark — the pre-theme default look: neutral dark background with
// the standard xterm 16-colour palette.
const TerminalTheme kClassicDark{
    "classic-dark", "themeClassicDark",
    0x0A0A0A, 0xFFFFFF, 0xFFFFFF, 0x803366CC,
    {0x000000, 0xCD0000, 0x00CD00, 0xCDCD00, 0x0000EE, 0xCD00CD, 0x00CDCD, 0xE5E5E5,
     0x7F7F7F, 0xFF0000, 0x00FF00, 0xFFFF00, 0x5C5CFF, 0xFF00FF, 0x00FFFF, 0xFFFFFF}
};

// Ocean Light — matching light variant
const TerminalTheme kOceanLight{
    "ocean-light", "themeOceanLight",
    0xF5F7FA, 0x232946, 0x3D5AFE, 0xCCE4ECF8,
    {0x3A3F55, 0xC93B57, 0x3E8E5A, 0xB07D1E, 0x2F5FD0, 0x8E4BB8, 0x1E8FA8, 0x5A6280,
     0x6B7194, 0xE0526B, 0x55B87E, 0xD9A23C, 0x5B82E0, 0xA86FD0, 0x3FB5C8, 0x232946}
};

// Solarized Dark — classic low-contrast scheme
const TerminalTheme kSolarizedDark{
    "solarized-dark", "themeSolarizedDark",
    0x002B36, 0x93A1A1, 0x268BD2, 0x803A5C73,
    {0x073642, 0xDC322F, 0x859900, 0xB58900, 0x268BD2, 0xD33682, 0x2AA198, 0xEEE8D5,
     0x002B36, 0xCB4B16, 0x586E75, 0x657B83, 0x839496, 0x6C71C4, 0x93A1A1, 0xFDF6E3}
};

// Dracula — purple-tinted dark scheme
const TerminalTheme kDracula{
    "dracula", "themeDracula",
    0x282A36, 0xF8F8F2, 0xBD93F9, 0x99447A86,
    {0x21222C, 0xFF5555, 0x50FA7B, 0xF1FA8C, 0xBD93F9, 0xFF79C6, 0x8BE9FD, 0xF8F8F2,
     0x6272A4, 0xFF6E6E, 0x69FF94, 0xFFFFA5, 0xD6ACFF, 0xFF92DF, 0xA4FFFF, 0xFFFFFF}
};

const std::vector<TerminalTheme> kThemes = {
    kOceanDark, kClassicDark, kOceanLight, kSolarizedDark, kDracula
};

} // namespace

uint32_t ResolveCellColor(uint32_t ref, const TerminalTheme& theme) {
    switch (ref >> 30) {
    case CELL_COLOR_INDEXED:
        return theme.ansi[ref & 0xFF];
    case CELL_COLOR_DEFAULT_FG:
        return theme.fg;
    case CELL_COLOR_DEFAULT_BG:
        return theme.bg;
    default:
        return ref & 0xFFFFFF;
    }
}

const std::vector<TerminalTheme>& GetTerminalThemes() {
    return kThemes;
}

const TerminalTheme& GetTerminalTheme(const std::string& id) {
    for (const auto& t : kThemes) {
        if (t.id == id) return t;
    }
    return kThemes.front();
}
