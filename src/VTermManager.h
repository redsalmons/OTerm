#ifndef VTERMMANAGER_H
#define VTERMMANAGER_H

#include <vterm.h>
#include <vector>
#include <string>
#include <functional>
#include <ctime>

class VTermManager {
public:
    // Terminal cell structure
    struct TerminalCell {
        uint32_t char_code;      // Unicode character code
        uint32_t fg_color;       // Foreground color (RGBA)
        uint32_t bg_color;       // Background color (RGBA)
        uint8_t attrs;           // Text attributes (bold, underline, etc.)
        bool dirty;               // Flag for dirty rectangle tracking
        uint8_t width;           // Cell width (1 for normal, 2 for wide characters like Chinese)
    };

    // Callback type for damage notifications
    using DamageCallback = std::function<void(VTermRect rect, const std::vector<std::vector<TerminalCell>>& cells)>;
    
    // Callback type for title changes
    using TitleCallback = std::function<void(const std::string& title)>;
    
    // Scroll history line structure
    struct HistoryLine {
        std::vector<TerminalCell> cells;  // Complete line of cells
        int timestamp;                    // When this line was added
    };

public:
    VTermManager();
    ~VTermManager();

    // Initialize VTerm with specified dimensions
    bool initialize(int rows, int cols);
    
    // Resize terminal to new dimensions
    void resize(int new_rows, int new_cols);
    
    // Write data to VTerm input (from SSH)
    int write_input(const char* data, int length);
    
    // Get terminal dimensions
    void get_size(int& rows, int& cols) const { rows = rows_; cols = cols_; }
    
    // Get cell buffer for rendering
    const std::vector<std::vector<TerminalCell>>& get_cell_buffer() const { return cell_buffer_; }
    
    // Check if VTerm is initialized
    bool is_initialized() const { return vt_ != nullptr && vts_ != nullptr; }
    
    // Set damage callback
    void set_damage_callback(DamageCallback callback) { damage_callback_ = callback; }
    
    // Set title callback
    void set_title_callback(TitleCallback callback) { title_callback_ = callback; }
    
    // Reset terminal
    void reset();
    
    // Cleanup VTerm resources
    void cleanup();
    
    // Helper function to convert ANSI color index to RGB
    static uint32_t ansi_color_to_rgb(uint8_t color_index);
    
    // Scroll history management
    void add_line_to_history(const VTermScreenCell* cells, int cols);
    void save_top_row_to_history();
    void save_row_to_history(int row_index);
    const std::vector<HistoryLine>& get_scroll_history() const { return scroll_history_; }
    void clear_scroll_history();
    
    // Screen access methods for scrolling
    const std::vector<TerminalCell>& get_screen_row(int row) const;
    int get_rows() const { return rows_; }
    int get_cols() const { return cols_; }
    
    // Auto-scroll methods
    int get_scroll_offset() const { return current_scroll_offset_; }
    void set_scroll_offset(int offset) { current_scroll_offset_ = offset; }
    
    // Cursor methods
    VTermPos get_cursor_pos() const;
    
    // Screen mode methods
    bool is_in_alternate_screen() const { return in_alternate_screen_; }
    
    // Scroll control methods
    void scroll_up(int lines = 1);
    void scroll_down(int lines = 1);
    void scroll_to_top();
    void scroll_to_bottom();
    bool can_scroll_up() const;
    bool can_scroll_down() const;
    
    // Output tracking for auto-scroll
    bool has_new_output() const;
    void mark_output_consumed();
    
private:
    // VTerm callback functions
    static int vterm_damage_callback(VTermRect rect, void *user);
    static int vterm_movecursor_callback(VTermPos pos, VTermPos oldpos, int visible, void *user);
    static int vterm_settermprop_callback(VTermProp prop, VTermValue *val, void *user);
    static int vterm_bell_callback(void *user);
    static int vterm_resize_callback(int rows, int cols, void *user);
    static int vterm_sb_pushline_callback(int cols, const VTermScreenCell *cells, void *user);

    // Internal helper methods
    void update_cell_buffer(VTermRect rect);
    void initialize_cell_buffer();
    void refresh_cell_buffer();

private:
    // VTerm components
    VTerm* vt_;
    VTermScreen* vts_;
    
    // Terminal dimensions
    int rows_;
    int cols_;
    
    // Screen cell buffer
    std::vector<std::vector<TerminalCell>> cell_buffer_;
    
    // Scroll history buffer (max 10000 lines)
    std::vector<HistoryLine> scroll_history_;
    static const int MAX_HISTORY_LINES = 10000;
    
    // Output tracking for auto-scroll
    bool has_new_output_;
    
    // Manual scrollback tracking
    int current_scroll_offset_;
    bool scrollback_triggered_;
    VTermPos last_cursor_pos_;
    int total_damage_rows_;
    int last_saved_row_;
    
    // Screen mode tracking
    bool in_alternate_screen_;
    bool entering_alternate_screen_;  // Flag to block history during alternate screen entry
    bool initializing_;  // Flag to prevent event processing during initialization
    
    // Callbacks
    DamageCallback damage_callback_;
    TitleCallback title_callback_;
};

#endif // VTERMMANAGER_H
