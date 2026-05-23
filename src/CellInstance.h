#pragma once

// Cell instance structure for terminal rendering
struct CellInstance {
    float cell_x, cell_y;        // Cell position in grid coordinates
    float uv_u, uv_v;            // Glyph UV coordinates in texture atlas
    float uv_w, uv_h;            // Glyph width and height in texture atlas
    uint32_t fg_color;           // Foreground color (RGBA)
    uint32_t bg_color;           // Background color (RGBA)
    uint32_t char_code;          // Unicode character code
};
