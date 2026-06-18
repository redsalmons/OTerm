#include "VTermManager.h"
#include "SSHManager.h"
#include <cstring>
#include <chrono>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

VTermManager::VTermManager()
    : vt_(nullptr), vts_(nullptr), rows_(10), cols_(80), has_new_output_(false), 
      current_scroll_offset_(0), scrollback_triggered_(false), total_damage_rows_(0), last_saved_row_(-1),
      in_alternate_screen_(false), entering_alternate_screen_(false), initializing_(true) {
    // Initialize scroll history as empty - using smaller size for easier scroll testing
    last_cursor_pos_ = { .row = 0, .col = 0 };
}

VTermManager::~VTermManager() {
    cleanup();
}

bool VTermManager::initialize(int rows, int cols) {
    rows_ = rows;
    cols_ = cols;

    // Initialize cell buffer first
    initialize_cell_buffer();

    // Create VTerm instance
    vt_ = vterm_new(rows, cols);
    if (!vt_) {
        return false;
    }

    // Set UTF8 mode
    vterm_set_utf8(vt_, 1);

    // Obtain screen
    vts_ = vterm_obtain_screen(vt_);
    if (!vts_) {
        vterm_free(vt_);
        vt_ = nullptr;
        return false;
    }

    // CRITICAL: Enable alternate screen support - required for vim detection
    vterm_screen_enable_altscreen(vts_, 1);

    // Reset the screen to clean state FIRST
    vterm_screen_reset(vts_, 1);

    // Set up screen callbacks including settermprop for vim detection
    static VTermScreenCallbacks screen_callbacks = {
        .damage = vterm_damage_callback,  // Essential for rendering
        .moverect = nullptr,
        .movecursor = vterm_movecursor_callback,  // Restore cursor tracking
        .settermprop = vterm_settermprop_callback,  // ALTSCREEN detection for vim
        .bell = nullptr,
        .resize = nullptr,
        .sb_pushline = vterm_sb_pushline_callback,  // Restore scrollback functionality
        .sb_popline = nullptr,
        .sb_clear = nullptr
    };

    // Register screen callbacks AFTER reset
    vterm_screen_set_callbacks(vts_, &screen_callbacks, this);

    // Initialization complete - enable event processing
    initializing_ = false;

    return true;
}

int VTermManager::write_input_no_flush(const char* data, int length) {
    if (!vt_ || !data || length <= 0) return -1;
    return vterm_input_write(vt_, data, length);
}

void VTermManager::flush_damage() {
    if (vts_) {
        vterm_screen_flush_damage(vts_);
    }
}

int VTermManager::write_input(const char* data, int length) {
    if (!vt_) {
        return -1;
    }

    if (!data || length <= 0) {
        return -1;
    }

    // Append new data to escape buffer for sequence detection
    escape_buffer_.append(data, length);
    
    // Check for alternate screen escape sequences in the buffer
    const char* buf = escape_buffer_.c_str();
    size_t buf_len = escape_buffer_.length();
    
    // ESC [ ? 1 0 4 9 h - enter alternate screen
    const char* enter_seq = "\x1b[?1049h";
    size_t enter_seq_len = 8;
    
    // ESC [ ? 1 0 4 9 l - exit alternate screen  
    const char* exit_seq = "\x1b[?1049l";
    size_t exit_seq_len = 8;
    
    // Check for enter sequence
    if (buf_len >= enter_seq_len) {
        for (size_t i = 0; i <= buf_len - enter_seq_len; i++) {
            if (memcmp(buf + i, enter_seq, enter_seq_len) == 0) {
                SSH_LOG("VTermManager::write_input: DETECTED ALTERNATE SCREEN ENTER (ESC [ ? 1049 h)");
                entering_alternate_screen_ = true;
                in_alternate_screen_ = true;
                // Remove the sequence from buffer
                escape_buffer_.erase(i, enter_seq_len);
                break;
            }
        }
    }
    
    // Check for exit sequence
    if (buf_len >= exit_seq_len) {
        for (size_t i = 0; i <= buf_len - exit_seq_len; i++) {
            if (memcmp(buf + i, exit_seq, exit_seq_len) == 0) {
                SSH_LOG("VTermManager::write_input: DETECTED ALTERNATE SCREEN EXIT (ESC [ ? 1049 l)");
                in_alternate_screen_ = false;
                entering_alternate_screen_ = false;  // Reset entering flag as well
                // Remove the sequence from buffer
                escape_buffer_.erase(i, exit_seq_len);
                break;
            }
        }
    }
    
    // Keep buffer size reasonable (keep last 100 chars)
    if (escape_buffer_.length() > 100) {
        escape_buffer_ = escape_buffer_.substr(escape_buffer_.length() - 100);
    }

    int result = vterm_input_write(vt_, data, length);

    // Ensure proper data synchronization by flushing damage
    if (vts_) {
        vterm_screen_flush_damage(vts_);
    }

    return result;
}

void VTermManager::resize(int new_rows, int new_cols) {
    SSH_LOG("VTermManager::resize called: " << new_rows << "x" << new_cols);

    if (!vt_) {
        SSH_LOG("VTermManager::resize: vt_ is null, returning");
        return;
    }

    if (new_rows == rows_ && new_cols == cols_) {
        SSH_LOG("VTermManager::resize: No change needed");
        return; // No change needed
    }

    // 1. Tell libvterm internal state changed
    vterm_set_size(vt_, new_rows, new_cols);
    SSH_LOG("VTermManager::resize: vterm_set_size completed");

    // 2. Note: vterm_screen_set_size doesn't exist in this version
    // The screen size is automatically updated by vterm_set_size

    // 3. Update rows_ and cols_ after vterm_set_size
    rows_ = new_rows;
    cols_ = new_cols;

    // 4. Reallocate cell_buffer_ size
    cell_buffer_.resize(rows_);
    for (auto& row : cell_buffer_) {
        row.resize(cols_);
    }
    SSH_LOG("VTermManager::resize: cell_buffer_ resized to " << rows_ << "x" << cols_ << ", actual size=" << cell_buffer_.size() << "x" << (cell_buffer_.empty() ? 0 : cell_buffer_[0].size()));
    
    // 4. Refresh cell_buffer_ from VTerm screen
    refresh_cell_buffer();

    // 4. Note: For SSH connections, PTY resize is handled by SSH channel
    // This would be used for local PTY connections
    // TODO: Add SSH channel resize notification if needed
    // if (pty_master_fd_ >= 0) {
    //     struct winsize ws;
    //     ws.ws_row = rows_;
    //     ws.ws_col = cols_;
    //     ws.ws_xpixel = 0;
    //     ws.ws_ypixel = 0;
    //     ioctl(pty_master_fd_, TIOCSWINSZ, &ws);
    // }

    SSH_LOG("VTermManager::resize completed successfully");
}

uint32_t VTermManager::ansi_color_to_rgb(uint8_t color_index) {
    // Standard 16-color ANSI palette
    static const uint32_t ansi_colors[16] = {
        0xFF000000, // Black
        0xFF800000, // Red
        0xFF008000, // Green
        0xFF808000, // Yellow
        0xFF000080, // Blue
        0xFF800080, // Magenta
        0xFF008080, // Cyan
        0xFFC0C0C0, // White
        0xFF808080, // Bright Black (Gray)
        0xFFFF0000, // Bright Red
        0xFF00FF00, // Bright Green
        0xFFFFFF00, // Bright Yellow
        0xFF0000FF, // Bright Blue
        0xFFFF00FF, // Bright Magenta
        0xFF00FFFF, // Bright Cyan
        0xFFFFFFFF  // Bright White
    };
    
    if (color_index < 16) {
        return ansi_colors[color_index];
    }
    
    // Default to white for unknown colors
    return 0xFFFFFFFF;
}

void VTermManager::reset() {
    if (vts_) {
        vterm_screen_reset(vts_, 1);
    }
}

void VTermManager::cleanup() {
    if (vt_) {
        vterm_free(vt_);
        vt_ = nullptr;
    }
    vts_ = nullptr;
}

int VTermManager::vterm_damage_callback(VTermRect rect, void *user) {
    VTermManager* manager = static_cast<VTermManager*>(user);

    // Update cell buffer for damaged region
    manager->update_cell_buffer(rect);
    
    // Check for shell prompt to trigger screen cleanup (vim exit detection)
    // Simple and safe approach: only check when we have valid data
    if (rect.start_row == 0 && manager->cell_buffer_.size() > 0 && manager->cell_buffer_[0].size() > 0) {
        const auto& first_cell = manager->cell_buffer_[0][0];
        // Check for common shell prompt characters at the very beginning
        if (first_cell.char_code == '$' || first_cell.char_code == '#' || first_cell.char_code == '~') {
            static int last_cleanup_frame = -1;
            static int frame_counter = 0;
            frame_counter++;
            
            // Only cleanup once per 100 frames to avoid excessive redraws
            if (last_cleanup_frame < frame_counter - 100) {
                // Force full screen redraw to clear any remaining vim content
                VTermPos end_pos = {manager->rows_ - 1, manager->cols_ - 1};
                VTermRect full_screen = {0, 0, end_pos.row, end_pos.col};
                manager->update_cell_buffer(full_screen);
                
                // Trigger damage callback to force full redraw
                if (manager->damage_callback_) {
                    manager->damage_callback_(full_screen, manager->cell_buffer_);
                }
                
                last_cleanup_frame = frame_counter;
            }
        }
    }
    
    // Trigger damage callback to force redraw
    if (manager->damage_callback_) {
        manager->damage_callback_(rect, manager->cell_buffer_);
    }
    
    return 1;
}

int VTermManager::vterm_movecursor_callback(VTermPos pos, VTermPos oldpos, int visible, void *user) {
    VTermManager* manager = static_cast<VTermManager*>(user);
    manager->last_cursor_pos_ = pos;
    return 1;
}

int VTermManager::vterm_settermprop_callback(VTermProp prop, VTermValue *val, void *user) {
    VTermManager* manager = static_cast<VTermManager*>(user);
    
    switch (prop) {
        case VTERM_PROP_ALTSCREEN:  // ALTSCREEN - Critical for vim detection
            SSH_LOG("vterm_settermprop_callback: ALTSCREEN prop changed, value=" << val->boolean << ", initializing=" << manager->initializing_ << ", current in_alternate_screen_=" << manager->in_alternate_screen_);
            // Skip processing during initialization
            if (manager->initializing_) {
                SSH_LOG("vterm_settermprop_callback: skipped due to initialization");
                break;
            }
            
            // Only process if state actually changes (avoid duplicate events)
            if (manager->in_alternate_screen_ != val->boolean) {
                // Update screen mode tracking
                manager->in_alternate_screen_ = val->boolean;
                SSH_LOG("vterm_settermprop_callback: in_alternate_screen_ updated to " << manager->in_alternate_screen_ << " (was " << !val->boolean << ")");
                
                if (val->boolean) {
                    // Entered alternate screen (top, vim, etc.)
                    SSH_LOG("vterm_settermprop_callback: Entered alternate screen mode - will skip history logging");
                    // Clear the entering flag since we're now confirmed to be in alternate screen
                    manager->entering_alternate_screen_ = false;
                } else {
                    // Exited alternate screen
                    SSH_LOG("vterm_settermprop_callback: Exited alternate screen mode - will resume history logging");
                    manager->entering_alternate_screen_ = false;  // Reset entering flag
                    
                    // When exiting from alternate screen, reset scroll offset
                    manager->current_scroll_offset_ = 0;
                    
                    // Force a full screen damage to ensure proper redraw
                    VTermPos end_pos = {manager->rows_ - 1, manager->cols_ - 1};
                    VTermRect full_screen = {0, 0, end_pos.row, end_pos.col};
                    manager->update_cell_buffer(full_screen);
                    
                    // Trigger damage callback to force full redraw
                    if (manager->damage_callback_) {
                        manager->damage_callback_(full_screen, manager->cell_buffer_);
                    }
                }
            }
            break;
        case VTERM_PROP_CURSORVISIBLE:
            break;
        case VTERM_PROP_TITLE:
            if (val->string.str) {
                std::string title(val->string.str, val->string.len);
                if (manager->title_callback_) {
                    manager->title_callback_(title);
                }
            }
            break;
        default:
            break;
    }
    
    return 1;
}

int VTermManager::vterm_bell_callback(void *user) {
    // Handle bell callback
    (void)user;
    return 1;
}

int VTermManager::vterm_resize_callback(int rows, int cols, void *user) {
    // Handle resize callback
    (void)rows;
    (void)cols;
    (void)user;
    return 1;
}

int VTermManager::vterm_sb_pushline_callback(int cols, const VTermScreenCell *cells, void *user) {
    VTermManager* manager = static_cast<VTermManager*>(user);
    if (!manager) {
        return 0; // Return failure to stop further callbacks
    }
    
    // Validate input parameters
    if (!cells || cols <= 0) {
        return 0; // Return failure
    }
    
    // Additional safety check for manager state
    if (!manager->is_initialized()) {
        return 0;
    }
    
    // Check if we're in alternate screen mode or entering it - if so, don't add to history
    if (manager->in_alternate_screen_ || manager->entering_alternate_screen_) {
        return 1; // Return success but don't add to history
    }
    
    // Add to history with error handling
    try {
        manager->add_line_to_history(cells, cols);
        manager->scrollback_triggered_ = true;
        
        // Auto-scroll to bottom if user hasn't manually scrolled
        // If current_scroll_offset_ is 0, user is at bottom, so keep them there
        if (manager->current_scroll_offset_ == 0) {
            // Keep at bottom (offset 0) - no change needed
        } else {
            // User has scrolled up, don't auto-scroll
        }
    } catch (const std::exception& e) {
        return 0; // Return failure
    }
    
    return 1; // Return success to continue receiving callbacks
}

void VTermManager::update_cell_buffer(VTermRect rect) {
    // Mark that we have new output for auto-scroll
    has_new_output_ = true;

    // Obtain VTerm state once outside the loop to avoid repeated calls
    VTermState* state = vterm_obtain_state(vt_);
    if (!state) return;
    VTermColor def_fg, def_bg;
    vterm_state_get_default_colors(state, &def_fg, &def_bg);
    
    // Update cells in the damaged region
    for (int row = rect.start_row; row < rect.end_row && row < rows_; row++) {
        for (int col = rect.start_col; col < rect.end_col && col < cols_; col++) {
            VTermPos pos = { .row = row, .col = col };
            
            // Get detailed cell information from libvterm
            VTermScreenCell cell;
            int result = vterm_screen_get_cell(vts_, pos, &cell);
            if (result != 1) {
                continue; // Skip if cell retrieval failed
            }
            
            // Extract character code (handle multi-byte Unicode)
            uint32_t char_code = 0;
            
            // Check cell width to determine if this is a wide character cell
            // For wide characters (like Chinese), only the first cell has the actual character code
            // Subsequent cells are part of the same character but have empty chars[0] (0 or 0xffffffff)
            if (cell.chars[0] != 0 && cell.chars[0] != 0xffffffff) {
                char_code = cell.chars[0];
            } else {
                // This is a continuation cell for a wide character
                // Set char_code to 0 so it won't be rendered
                char_code = 0;
            }
            
            
            // Extract colors using VTerm's actual color structure
            uint32_t fg_color = 0xffffffff; // Default white
            uint32_t bg_color = 0x00000000; // Default black
            
            // Convert foreground color from VTermColor to RGB (ABGR format for ImGui)
            VTermColor vt_fg = cell.fg;
            vterm_state_convert_color_to_rgb(state, &vt_fg);
            // ABGR format: Alpha in high bits, then Blue, Green, Red in low bits
            fg_color = (0xFF << 24) | 
                       ((uint32_t)vt_fg.rgb.blue << 16) | 
                       ((uint32_t)vt_fg.rgb.green << 8) | 
                       (uint32_t)vt_fg.rgb.red;
            
            // Convert background color from VTermColor to RGB (ABGR format for ImGui)
            VTermColor vt_bg = cell.bg;
            vterm_state_convert_color_to_rgb(state, &vt_bg);
            
            // Core: Handle default background transparency
            if (vt_bg.rgb.red == def_bg.rgb.red && 
                vt_bg.rgb.green == def_bg.rgb.green && 
                vt_bg.rgb.blue == def_bg.rgb.blue) {
                // Default background = dark gray (matches terminal background)
                bg_color = (0xFF << 24) | (0x0A << 16) | (0x0A << 8) | 0x0A;
            } else {
                bg_color = (0xFF << 24) | 
                           ((uint32_t)vt_bg.rgb.blue << 16) | 
                           ((uint32_t)vt_bg.rgb.green << 8) | 
                           (uint32_t)vt_bg.rgb.red;
            }
            
            // Debug: Print actual RGB values (commented out to reduce noise)
            // if (row == 0 && col < 3) {
            //     std::cout << "COLOR DEBUG: R=" << (int)vt_fg.rgb.red 
            //               << " G=" << (int)vt_fg.rgb.green 
            //               << " B=" << (int)vt_fg.rgb.blue 
            //               << " fg_color=0x" << std::hex << fg_color 
            //               << " bg_color=0x" << bg_color << std::dec << std::endl;
            // }
            
                        
            // Handle reverse attribute
            if (cell.attrs.reverse) {
                std::swap(fg_color, bg_color);
            }
            
            // Note: reverse attribute already handled above
            
            // Extract attributes
            uint8_t attrs = 0;
            if (cell.attrs.bold) attrs |= 0x01;
            if (cell.attrs.underline) attrs |= 0x02;
            if (cell.attrs.italic) attrs |= 0x04;
            if (cell.attrs.blink) attrs |= 0x08;
            if (cell.attrs.reverse) attrs |= 0x10;
            
            // Update cell buffer
            if (row < cell_buffer_.size() && col < cell_buffer_[row].size()) {
                cell_buffer_[row][col].char_code = char_code;
                cell_buffer_[row][col].fg_color = fg_color;
                cell_buffer_[row][col].bg_color = bg_color;
                cell_buffer_[row][col].attrs = attrs;
                cell_buffer_[row][col].width = cell.width;
                cell_buffer_[row][col].dirty = true;
                
                // Debug output for first few cells (commented out to reduce noise)
                // if (row == 0 && col < 5) {
                //     std::cout << "CELL[" << row << "," << col << "]: char='" 
                //               << (char)(char_code < 128 ? char_code : '?') 
                //               << "' fg=0x" << std::hex << fg_color 
                //               << " bg=0x" << bg_color << " attrs=0x" << attrs << std::dec << std::endl;
                // }
            }
        }
    }
}

void VTermManager::refresh_cell_buffer() {
    if (!vts_) return;
    
    SSH_LOG("refresh_cell_buffer: reading entire screen from VTerm, rows=" << rows_ << ", cols=" << cols_);
    
    // Read entire screen from VTerm
    VTermRect full_screen = {0, 0, cols_, rows_};
    update_cell_buffer(full_screen);
    
    // Log sample cells to verify data was read
    if (cell_buffer_.size() > 0 && cell_buffer_[0].size() > 0) {
        SSH_LOG("refresh_cell_buffer: sample cell[0][0] char_code=" << cell_buffer_[0][0].char_code << ", fg_color=0x" << std::hex << cell_buffer_[0][0].fg_color << std::dec);
    }
    if (cell_buffer_.size() > 1 && cell_buffer_[1].size() > 0) {
        SSH_LOG("refresh_cell_buffer: sample cell[1][0] char_code=" << cell_buffer_[1][0].char_code);
    }
    if (cell_buffer_.size() > 44 && cell_buffer_[44].size() > 0) {
        SSH_LOG("refresh_cell_buffer: sample cell[44][0] char_code=" << cell_buffer_[44][0].char_code);
    }
    if (cell_buffer_.size() > 81 && cell_buffer_[81].size() > 0) {
        SSH_LOG("refresh_cell_buffer: sample cell[81][0] char_code=" << cell_buffer_[81][0].char_code);
    } else {
        SSH_LOG("refresh_cell_buffer: cell_buffer_ size=" << cell_buffer_.size() << ", cannot access cell[81]");
    }
}

void VTermManager::initialize_cell_buffer() {
    
    // Initialize cell buffer with empty cells
    cell_buffer_.resize(rows_);
    for (int row = 0; row < rows_; row++) {
        cell_buffer_[row].resize(cols_);
        for (int col = 0; col < cols_; col++) {
            cell_buffer_[row][col] = {
                .char_code = ' ',
                .fg_color = 0xffffffff,
                .bg_color = 0x00000000,
                .attrs = 0x0,
                .dirty = false,
                .width = 1
            };
        }
    }
}
void VTermManager::add_line_to_history(const VTermScreenCell* cells, int cols) {
    // Safety checks
    if (!cells || cols <= 0 || cols > 1000) {
        return;
    }
    
    // Create a new history line
    HistoryLine history_line;
    history_line.timestamp = time(nullptr);
    history_line.cells.resize(cols);
    
    // Convert VTermScreenCell to TerminalCell format
    for (int col = 0; col < cols; col++) {
        TerminalCell& terminal_cell = history_line.cells[col];
        
        if (cells[col].chars[0] != 0) {
            terminal_cell.char_code = cells[col].chars[0];
        } else {
            terminal_cell.char_code = ' ';
        }
        terminal_cell.width = cells[col].width;
        
        // Extract colors using VTerm's actual color structure
        uint32_t fg_color = 0xffffffff; // Default white
        uint32_t bg_color = 0x00000000; // Default black
        
        // Use proper ANSI color extraction like in the other function
        // Note: This is a simplified version - full implementation would need VTerm color access
        // For now, use basic colors to avoid the red-only issue
        if (cells[col].attrs.bold) {
            fg_color = 0xFFFFFFFF; // Keep white for bold
        }
        if (cells[col].attrs.reverse) {
            // Swap foreground and background for reverse video
            uint32_t temp = fg_color;
            fg_color = bg_color;
            bg_color = temp;
        }
        
        terminal_cell.fg_color = fg_color;
        terminal_cell.bg_color = bg_color;
        
        // Extract attributes
        uint8_t attrs = 0;
        if (cells[col].attrs.bold) attrs |= 0x01;
        if (cells[col].attrs.underline) attrs |= 0x02;
        if (cells[col].attrs.italic) attrs |= 0x04;
        if (cells[col].attrs.blink) attrs |= 0x08;
        if (cells[col].attrs.reverse) attrs |= 0x10;
        
        terminal_cell.attrs = attrs;
        terminal_cell.dirty = false; // History lines don't need dirty tracking
    }
    
    // Add to history buffer
    scroll_history_.push_back(history_line);
    
    // Maintain maximum history size (10000 lines)
    if (scroll_history_.size() > MAX_HISTORY_LINES) {
        scroll_history_.pop_front();
    }
}

void VTermManager::save_top_row_to_history() {
    if (!vts_ || cell_buffer_.empty()) {
        return;
    }
    
    // Don't save history in alternate screen mode (e.g., vi, less)
    if (in_alternate_screen_) {
        SSH_LOG("save_top_row_to_history: skipped (in alternate screen mode)");
        return;
    }
    
    SSH_LOG("save_top_row_to_history: saving top row to history, history size: " << scroll_history_.size());
    
    // Get the top row (row 0) before it gets scrolled off
    std::vector<TerminalCell> top_row = cell_buffer_[0];
    
    // Create a history line
    HistoryLine history_line;
    history_line.timestamp = time(nullptr);
    history_line.cells = top_row;
    
    // Add to history buffer
    scroll_history_.push_back(history_line);
    
    // Maintain maximum history size
    if (scroll_history_.size() > MAX_HISTORY_LINES) {
        scroll_history_.pop_front();
    }
}

void VTermManager::save_row_to_history(int row_index) {
    if (!vts_ || cell_buffer_.empty() || row_index < 0 || row_index >= cell_buffer_.size()) {
        return;
    }
    
    // Don't save history in alternate screen mode (e.g., vi, less)
    if (in_alternate_screen_) {
        SSH_LOG("save_row_to_history: skipped (in alternate screen mode)");
        return;
    }
    
    SSH_LOG("save_row_to_history: saving row " << row_index << " to history, history size: " << scroll_history_.size());
    
    // Get the specified row
    std::vector<TerminalCell> row = cell_buffer_[row_index];
    
    // Create a history line
    HistoryLine history_line;
    history_line.timestamp = time(nullptr);
    history_line.cells = row;
    
    // Add to history buffer
    scroll_history_.push_back(history_line);
    
    // Maintain maximum history size
    if (scroll_history_.size() > MAX_HISTORY_LINES) {
        scroll_history_.pop_front();
    }
}

void VTermManager::clear_scroll_history() {
    scroll_history_.clear();
}
const std::vector<VTermManager::TerminalCell>& VTermManager::get_screen_row(int row) const {
    // In alternate screen (vi mode), ignore scroll offset - no scrollback
    if (!in_alternate_screen_ && current_scroll_offset_ > 0) {
        // When scrolling up (current_scroll_offset_ > 0), show history at the top
        // Calculate how many history lines to show
        int history_lines_to_show = (current_scroll_offset_ < (int)scroll_history_.size()) ? current_scroll_offset_ : (int)scroll_history_.size();
        
        // If this row is in the history section
        if (row < history_lines_to_show) {
            // Get from history (most recent history first)
            int history_index = scroll_history_.size() - history_lines_to_show + row;
            if (history_index >= 0 && history_index < (int)scroll_history_.size()) {
                return scroll_history_[history_index].cells;
            }
        }
        // Otherwise get from current screen (shifted down)
        else {
            int screen_row = row - history_lines_to_show;
            if (screen_row >= 0 && screen_row < rows_ && screen_row < (int)cell_buffer_.size()) {
                return cell_buffer_[screen_row];
            }
        }
    }
    // Not scrolling or in alternate screen, get from current screen normally
    else if (row >= 0 && row < rows_ && row < (int)cell_buffer_.size()) {
        return cell_buffer_[row];
    }
    
    // Return empty row if out of bounds
    static std::vector<TerminalCell> empty_row;
    return empty_row;
}

bool VTermManager::has_new_output() const {
    return has_new_output_;
}

void VTermManager::mark_output_consumed() {
    has_new_output_ = false;
}

void VTermManager::scroll_up(int lines) {
    int total_lines = scroll_history_.size() + rows_;
    int max_offset = total_lines - rows_; // Maximum scroll offset (showing oldest content)
    
    // Safety check - if total_lines is less than or equal to rows_, we cannot scroll up
    if (max_offset <= 0) {
        current_scroll_offset_ = 0;
        return;
    }
    
    int old_offset = current_scroll_offset_;
    current_scroll_offset_ = (std::min)(current_scroll_offset_ + lines, max_offset);
    
    SSH_LOG("scroll_up: lines=" << lines << ", old_offset=" << old_offset << ", new_offset=" << current_scroll_offset_ << ", max_offset=" << max_offset << ", history_size=" << scroll_history_.size());
}

void VTermManager::scroll_down(int lines) {
    int old_offset = current_scroll_offset_;
    current_scroll_offset_ = (std::max)(current_scroll_offset_ - lines, 0);
    
    SSH_LOG("scroll_down: lines=" << lines << ", old_offset=" << old_offset << ", new_offset=" << current_scroll_offset_ << ", history_size=" << scroll_history_.size());
}

void VTermManager::scroll_to_top() {
    int total_lines = scroll_history_.size() + rows_;
    current_scroll_offset_ = (std::max)(0, total_lines - rows_);
}

void VTermManager::scroll_to_bottom() {
    current_scroll_offset_ = 0;
}

bool VTermManager::can_scroll_up() const {
    int total_lines = scroll_history_.size() + rows_;
    int max_offset = total_lines - rows_;
    return current_scroll_offset_ < max_offset;
}

bool VTermManager::can_scroll_down() const {
    return current_scroll_offset_ > 0;
}

VTermPos VTermManager::get_cursor_pos() const {
    VTermPos pos;
    if (vt_ && vts_) {
        VTermState* state = vterm_obtain_state(vt_);
        if (state) {
            vterm_state_get_cursorpos(state, &pos);
            return pos;
        }
    }
    // Fallback to cached position if libvterm is not available
    return last_cursor_pos_;
}
