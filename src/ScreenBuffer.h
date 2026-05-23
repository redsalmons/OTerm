#pragma once

#include <vector>
#include "CellInstance.h"

// Screen buffer for double buffering
// Used to safely share screen data between worker thread and UI thread
struct ScreenBuffer {
    std::vector<std::vector<CellInstance>> cells;
    int rows;
    int cols;
    int cursor_row;
    int cursor_col;
    
    ScreenBuffer() : rows(0), cols(0), cursor_row(0), cursor_col(0) {}
    
    ScreenBuffer(int r, int c) : rows(r), cols(c), cursor_row(0), cursor_col(0) {
        resize(r, c);
    }
    
    void resize(int r, int c) {
        rows = r;
        cols = c;
        cells.resize(r);
        for (int i = 0; i < r; i++) {
            cells[i].resize(c);
        }
    }
    
    void clear() {
        for (auto& row : cells) {
            for (auto& cell : row) {
                cell.cell_x = 0;
                cell.cell_y = 0;
                cell.uv_u = 0;
                cell.uv_v = 0;
                cell.uv_w = 0;
                cell.uv_h = 0;
                cell.fg_color = 0;
                cell.bg_color = 0;
                cell.char_code = 0;
            }
        }
    }
};
