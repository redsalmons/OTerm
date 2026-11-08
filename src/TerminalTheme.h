#ifndef TERMINALTHEME_H
#define TERMINALTHEME_H

#include <cstdint>
#include <string>
#include <vector>

// A terminal colour theme. All colours are 0xRRGGBB except selection which
// is 0xAARRGGBB (alpha in the top byte).
struct TerminalTheme {
    std::string id;          // stable identifier persisted in settings
    std::string nameKey;     // TranslationHelper key for the display name
    uint32_t bg;
    uint32_t fg;
    uint32_t cursor;
    uint32_t selection;
    uint32_t ansi[16];       // indices 0-7 normal, 8-15 bright
};

// Cell colour reference packing (stored in CellInstance::fg_color/bg_color):
//   bits 31-30: kind
//   RGB     -> bits 0-23 hold 0xRRGGBB
//   INDEXED -> bits 0-7  hold the ANSI palette index (0-15 only)
//   DEFAULT_FG / DEFAULT_BG carry no payload
enum CellColorKind : uint32_t {
    CELL_COLOR_RGB = 0,
    CELL_COLOR_INDEXED = 1,
    CELL_COLOR_DEFAULT_FG = 2,
    CELL_COLOR_DEFAULT_BG = 3
};

inline uint32_t PackCellColorRGB(uint32_t rgb) {
    return (CELL_COLOR_RGB << 30) | (rgb & 0xFFFFFF);
}
inline uint32_t PackCellColorIndexed(int index) {
    return (CELL_COLOR_INDEXED << 30) | (static_cast<uint32_t>(index) & 0xFF);
}
inline uint32_t PackCellColorDefault(uint32_t kind) {
    return kind << 30;
}

// Resolve a packed cell colour reference against a theme.
// Returns 0xRRGGBB.
uint32_t ResolveCellColor(uint32_t ref, const TerminalTheme& theme);

const std::vector<TerminalTheme>& GetTerminalThemes();
const TerminalTheme& GetTerminalTheme(const std::string& id);

#endif // TERMINALTHEME_H
