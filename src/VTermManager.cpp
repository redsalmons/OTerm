#include "VTermManager.h"
#include "SSHManager.h"
#include "TerminalTheme.h"
#include "GlobalConfig.h"
#include <cstring>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdlib>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <process.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#include <signal.h>
#endif

namespace {

// Pack a VTermScreenCell's colours as unresolved references; the renderer
// resolves them against the active theme so theme switches apply instantly.
void PackVTermCellColors(const VTermScreenCell& cell, VTermState* state,
                         uint32_t& fg_color, uint32_t& bg_color) {
    fg_color = PackCellColorDefault(CELL_COLOR_DEFAULT_FG);
    bg_color = PackCellColorDefault(CELL_COLOR_DEFAULT_BG);

    VTermColor vt_fg = cell.fg;
    if (VTERM_COLOR_IS_DEFAULT_FG(&vt_fg)) {
        fg_color = PackCellColorDefault(CELL_COLOR_DEFAULT_FG);
    } else if (VTERM_COLOR_IS_INDEXED(&vt_fg) && vt_fg.indexed.idx < 16) {
        fg_color = PackCellColorIndexed(vt_fg.indexed.idx);
    } else {
        vterm_state_convert_color_to_rgb(state, &vt_fg);
        fg_color = PackCellColorRGB((vt_fg.rgb.red << 16) |
                                    (vt_fg.rgb.green << 8) |
                                    vt_fg.rgb.blue);
    }

    VTermColor vt_bg = cell.bg;
    if (VTERM_COLOR_IS_DEFAULT_BG(&vt_bg)) {
        bg_color = PackCellColorDefault(CELL_COLOR_DEFAULT_BG);
    } else if (VTERM_COLOR_IS_INDEXED(&vt_bg) && vt_bg.indexed.idx < 16) {
        bg_color = PackCellColorIndexed(vt_bg.indexed.idx);
    } else {
        vterm_state_convert_color_to_rgb(state, &vt_bg);
        bg_color = PackCellColorRGB((vt_bg.rgb.red << 16) |
                                    (vt_bg.rgb.green << 8) |
                                    vt_bg.rgb.blue);
    }

    if (cell.attrs.reverse) {
        std::swap(fg_color, bg_color);
    }
}

// On-disk record for one history cell (14 bytes, packed).
#pragma pack(push, 1)
struct HistoryCellRecord {
    uint32_t char_code;
    uint32_t fg_color;
    uint32_t bg_color;
    uint8_t attrs;
    uint8_t width;
};
#pragma pack(pop)

} // namespace

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

    // Inject the active theme so OSC colour queries and palette-aware apps
    // see the same colours the renderer uses.
    ApplyThemeToVTerm();

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
    SyncThemeIfNeeded();
    return vterm_input_write(vt_, data, length);
}

void VTermManager::flush_damage() {
    SyncThemeIfNeeded();
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

    SyncThemeIfNeeded();
    int result = vterm_input_write(vt_, data, length);

    // Ensure proper data synchronization by flushing damage
    if (vts_) {
        vterm_screen_flush_damage(vts_);
    }

    return result;
}

void VTermManager::resize(int new_rows, int new_cols) {
    SSH_LOG("VTermManager::resize called: " << rows_ << "x" << cols_ << " -> " << new_rows << "x" << new_cols);

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
    SSH_LOG("VTermManager::resize: vterm_set_size (" << new_rows << "x" << new_cols << ") completed");

    // 2. Update rows_ and cols_
    rows_ = new_rows;
    cols_ = new_cols;

    // 3. Reallocate cell_buffer_ size
    cell_buffer_.resize(rows_);
    for (auto& row : cell_buffer_) {
        row.resize(cols_);
    }
    SSH_LOG("VTermManager::resize: cell_buffer_ resized. Internal state now: " << rows_ << "x" << cols_);
    
    // 4. Refresh cell_buffer_ from VTerm screen
    refresh_cell_buffer();

    SSH_LOG("VTermManager::resize completed successfully");
}

void VTermManager::reset() {
    if (vts_) {
        vterm_screen_reset(vts_, 1);
    }
}

void VTermManager::cleanup() {
    // The tab/session is going away: drop the scrollback cache file too.
    CloseHistoryFile();
    if (!history_path_.empty()) {
        std::error_code ec;
        std::filesystem::remove(history_path_, ec);
        history_path_.clear();
    }
    scroll_history_.clear();
    history_page_.clear();
    page_start_ = -1;
    history_total_ = 0;
    history_offsets_.clear();

    if (vt_) {
        vterm_free(vt_);
        vt_ = nullptr;
    }
    vts_ = nullptr;
}

void VTermManager::CleanupStaleScrollbackFiles() {
    const std::filesystem::path dir =
        std::filesystem::path(GlobalConfig::GetConfigPath()) / "scrollback";
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) return;

    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        const std::string name = entry.path().filename().string();
        // Expected name: scrollback_<pid>_<counter>.bin
        if (name.rfind("scrollback_", 0) != 0) continue;
        const size_t pid_begin = 11;  // strlen("scrollback_")
        const size_t pid_end = name.find('_', pid_begin);
        if (pid_end == std::string::npos) continue;
        const int pid = std::atoi(name.substr(pid_begin, pid_end - pid_begin).c_str());
        if (pid <= 0) continue;

        bool alive = false;
#ifdef _WIN32
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        alive = (h != nullptr);
        if (h) CloseHandle(h);
#else
        alive = (kill(pid, 0) == 0 || errno == EPERM);
#endif
        if (!alive) {
            std::filesystem::remove(entry.path(), ec);
        }
    }
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
                VTermRect full_screen = {0, manager->rows_, 0, manager->cols_};
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
    if (manager->damage_callback_) {
        VTermRect empty_rect = {0, 0, 0, 0};
        manager->damage_callback_(empty_rect, manager->cell_buffer_);
    }
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
                    VTermRect full_screen = {0, manager->rows_, 0, manager->cols_};
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

void VTermManager::SyncThemeIfNeeded() {
    if (vt_ && GlobalConfig::GetThemeName() != applied_theme_name_) {
        ApplyThemeToVTerm();
    }
}

void VTermManager::ApplyThemeToVTerm() {
    if (!vt_) return;
    VTermState* state = vterm_obtain_state(vt_);
    if (!state) return;

    const TerminalTheme& theme = GlobalConfig::GetTheme();
    for (int i = 0; i < 16; ++i) {
        const uint32_t c = theme.ansi[i];
        VTermColor col;
        col.type = VTERM_COLOR_RGB;
        col.rgb.red = (c >> 16) & 0xFF;
        col.rgb.green = (c >> 8) & 0xFF;
        col.rgb.blue = c & 0xFF;
        vterm_state_set_palette_color(state, i, &col);
    }
    VTermColor def_fg, def_bg;
    const uint32_t fg = theme.fg, bg = theme.bg;
    def_fg.type = VTERM_COLOR_RGB;
    def_fg.rgb.red = (fg >> 16) & 0xFF;
    def_fg.rgb.green = (fg >> 8) & 0xFF;
    def_fg.rgb.blue = fg & 0xFF;
    def_bg.type = VTERM_COLOR_RGB;
    def_bg.rgb.red = (bg >> 16) & 0xFF;
    def_bg.rgb.green = (bg >> 8) & 0xFF;
    def_bg.rgb.blue = bg & 0xFF;
    vterm_state_set_default_colors(state, &def_fg, &def_bg);

    applied_theme_name_ = GlobalConfig::GetThemeName();
}

void VTermManager::update_cell_buffer(VTermRect rect) {
    // Mark that we have new output for auto-scroll
    has_new_output_ = true;

    // Obtain VTerm state once outside the loop to avoid repeated calls
    VTermState* state = vterm_obtain_state(vt_);
    if (!state) return;

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
            
            
            // Pack colours as unresolved references; the renderer resolves them
            // against the active theme so theme switches apply instantly.
            uint32_t fg_color, bg_color;
            PackVTermCellColors(cell, state, fg_color, bg_color);
            
            // Debug: Print actual RGB values (commented out to reduce noise)
            // if (row == 0 && col < 3) {
            //     std::cout << "COLOR DEBUG: R=" << (int)vt_fg.rgb.red 
            //               << " G=" << (int)vt_fg.rgb.green 
            //               << " B=" << (int)vt_fg.rgb.blue 
            //               << " fg_color=0x" << std::hex << fg_color 
            //               << " bg_color=0x" << bg_color << std::dec << std::endl;
            // }
            
                        
            // Note: reverse attribute already handled in PackVTermCellColors

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
            }
        }
    }
}

void VTermManager::refresh_cell_buffer() {
    if (!vts_) return;
    
    // Read entire screen from VTerm
    VTermRect full_screen = {0, rows_, 0, cols_};
    update_cell_buffer(full_screen);
}

void VTermManager::initialize_cell_buffer() {
    
    // Initialize cell buffer with empty cells
    cell_buffer_.resize(rows_);
    for (int row = 0; row < rows_; row++) {
        cell_buffer_[row].resize(cols_);
        for (int col = 0; col < cols_; col++) {
            cell_buffer_[row][col] = {
                .char_code = ' ',
                .fg_color = PackCellColorDefault(CELL_COLOR_DEFAULT_FG),
                .bg_color = PackCellColorDefault(CELL_COLOR_DEFAULT_BG),
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

    VTermState* state = vterm_obtain_state(vt_);

    std::vector<TerminalCell> line;
    line.reserve(cols);
    for (int col = 0; col < cols; col++) {
        TerminalCell tc;
        tc.char_code = (cells[col].chars[0] != 0 && cells[col].chars[0] != 0xffffffff)
                           ? cells[col].chars[0]
                           : static_cast<uint32_t>(' ');
        tc.width = cells[col].width;

        if (state) {
            PackVTermCellColors(cells[col], state, tc.fg_color, tc.bg_color);
        } else {
            tc.fg_color = PackCellColorDefault(CELL_COLOR_DEFAULT_FG);
            tc.bg_color = PackCellColorDefault(CELL_COLOR_DEFAULT_BG);
        }

        uint8_t attrs = 0;
        if (cells[col].attrs.bold) attrs |= 0x01;
        if (cells[col].attrs.underline) attrs |= 0x02;
        if (cells[col].attrs.italic) attrs |= 0x04;
        if (cells[col].attrs.blink) attrs |= 0x08;
        if (cells[col].attrs.reverse) attrs |= 0x10;
        tc.attrs = attrs;
        tc.dirty = false;

        line.push_back(tc);
    }

    // Trim trailing cells that carry no information: blank text, default
    // background and no attributes. Consumers treat missing cells as blank.
    const uint32_t default_bg = PackCellColorDefault(CELL_COLOR_DEFAULT_BG);
    size_t last = line.size();
    while (last > 0) {
        const TerminalCell& c = line[last - 1];
        const bool meaningful = (c.char_code != ' ' && c.char_code != 0) ||
                                c.bg_color != default_bg || c.attrs != 0;
        if (meaningful) break;
        --last;
    }
    line.resize(last);

    AppendHistoryLine(std::move(line));
}

size_t VTermManager::HistoryTailCapacity() const {
    return static_cast<size_t>(std::max(120, rows_ * 3));
}

size_t VTermManager::HistoryPageCapacity() const {
    return static_cast<size_t>(std::max(120, rows_ * 3));
}

void VTermManager::EnsureHistoryFile() {
    if (history_file_) return;

    std::filesystem::path dir =
        std::filesystem::path(GlobalConfig::GetConfigPath()) / "scrollback";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    static std::atomic<int> counter{0};
#ifdef _WIN32
    const int pid = _getpid();
#else
    const int pid = getpid();
#endif
    char name[64];
    snprintf(name, sizeof(name), "scrollback_%d_%d.bin", pid, counter.fetch_add(1));
    history_path_ = (dir / name).string();
    history_file_ = fopen(history_path_.c_str(), "ab");
}

void VTermManager::CloseHistoryFile() {
    if (history_file_) {
        fclose(history_file_);
        history_file_ = nullptr;
    }
}

void VTermManager::AppendHistoryLine(std::vector<TerminalCell>&& cells) {
    std::lock_guard<std::mutex> lock(history_mutex_);
    EnsureHistoryFile();
    if (!history_file_) return;  // No backing store: drop the line

    if (history_total_ >= MAX_HISTORY_FILE_LINES) {
        CompactHistoryFile();
    }
    if (history_total_ >= MAX_HISTORY_FILE_LINES) return;

    const uint32_t ncols = static_cast<uint32_t>(cells.size());
    const uint64_t offset =
#ifdef _WIN32
        static_cast<uint64_t>(_ftelli64(history_file_));
#else
        static_cast<uint64_t>(ftello(history_file_));
#endif
    fwrite(&ncols, sizeof(ncols), 1, history_file_);
    for (const auto& c : cells) {
        HistoryCellRecord rec{c.char_code, c.fg_color, c.bg_color, c.attrs, c.width};
        fwrite(&rec, sizeof(rec), 1, history_file_);
    }
    history_offsets_.push_back(offset);
    ++history_total_;

    HistoryLine line;
    line.cells = std::move(cells);
    scroll_history_.push_back(std::move(line));

    const size_t cap = HistoryTailCapacity();
    while (scroll_history_.size() > cap) {
        scroll_history_.pop_front();
    }
}

void VTermManager::CompactHistoryFile() {
    // Keep the newest HISTORY_KEEP_LINES records; rewrite the log without
    // the dropped prefix and rebuild the offset index.
    CloseHistoryFile();
    history_page_.clear();
    page_start_ = -1;

    const int64_t drop = history_total_ - HISTORY_KEEP_LINES;
    if (drop <= 0) return;

    const std::string old_path = history_path_;
    const std::string tmp_path = old_path + ".tmp";

    std::ifstream in(old_path, std::ios::binary);
    std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
    if (!in || !out) {
        history_path_.clear();
        return;
    }

    std::vector<uint64_t> new_offsets;
    new_offsets.reserve(HISTORY_KEEP_LINES);
    uint64_t out_pos = 0;
    for (int64_t i = drop; i < history_total_; ++i) {
        in.seekg(static_cast<std::streamoff>(history_offsets_[i]));
        uint32_t ncols = 0;
        in.read(reinterpret_cast<char*>(&ncols), sizeof(ncols));
        if (!in) break;
        new_offsets.push_back(out_pos);
        out.write(reinterpret_cast<const char*>(&ncols), sizeof(ncols));
        out_pos += sizeof(ncols);
        std::vector<char> buf(static_cast<size_t>(ncols) * sizeof(HistoryCellRecord));
        in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        if (!in) break;
        out.write(buf.data(), static_cast<std::streamsize>(buf.size()));
        out_pos += static_cast<uint64_t>(buf.size());
    }
    in.close();
    out.close();

    std::error_code ec;
    std::filesystem::rename(tmp_path, old_path, ec);
    if (ec) {
        std::filesystem::remove(tmp_path, ec);
        history_path_.clear();
        return;
    }

    history_offsets_ = std::move(new_offsets);
    history_total_ = static_cast<int64_t>(history_offsets_.size());
    history_path_ = old_path;
    history_file_ = fopen(history_path_.c_str(), "ab");
}

void VTermManager::LoadHistoryRange(int64_t start, int64_t count) {
    std::lock_guard<std::mutex> lock(history_mutex_);
    history_page_.clear();
    page_start_ = -1;
    if (history_path_.empty() || start < 0 || start >= history_total_) return;

    std::ifstream in(history_path_, std::ios::binary);
    if (!in) return;

    const int64_t end = std::min(start + count, history_total_);
    count = end - start;
    if (count <= 0) return;

    history_page_.resize(static_cast<size_t>(count));
    for (int64_t i = 0; i < count; ++i) {
        in.seekg(static_cast<std::streamoff>(history_offsets_[start + i]));
        uint32_t ncols = 0;
        in.read(reinterpret_cast<char*>(&ncols), sizeof(ncols));
        if (!in) {
            history_page_.resize(static_cast<size_t>(i));
            break;
        }
        auto& cells = history_page_[static_cast<size_t>(i)].cells;
        cells.resize(ncols);
        for (uint32_t j = 0; j < ncols; ++j) {
            HistoryCellRecord rec;
            in.read(reinterpret_cast<char*>(&rec), sizeof(rec));
            if (!in) {
                cells.resize(j);
                break;
            }
            TerminalCell& tc = cells[j];
            tc.char_code = rec.char_code;
            tc.fg_color = rec.fg_color;
            tc.bg_color = rec.bg_color;
            tc.attrs = rec.attrs;
            tc.width = rec.width;
            tc.dirty = false;
        }
    }
    page_start_ = start;
}

void VTermManager::EnsureHistoryPage(int64_t index) {
    if (page_start_ >= 0 && index >= page_start_ &&
        index < page_start_ + static_cast<int64_t>(history_page_.size())) {
        return;
    }
    const int64_t cap = static_cast<int64_t>(HistoryPageCapacity());
    const int64_t max_start = std::max<int64_t>(0, history_total_ - cap);
    int64_t start = index - cap / 2;
    start = std::max<int64_t>(0, std::min(start, max_start));
    LoadHistoryRange(start, std::min(cap, history_total_ - start));
}

const std::vector<VTermManager::TerminalCell>& VTermManager::GetHistoryLine(int64_t index) {
    const int64_t tail_start = history_total_ - static_cast<int64_t>(scroll_history_.size());
    if (index >= tail_start) {
        return scroll_history_[static_cast<size_t>(index - tail_start)].cells;
    }
    EnsureHistoryPage(index);
    if (page_start_ >= 0 && index >= page_start_ &&
        index < page_start_ + static_cast<int64_t>(history_page_.size())) {
        return history_page_[static_cast<size_t>(index - page_start_)].cells;
    }
    static std::vector<TerminalCell> empty;
    return empty;
}

const std::vector<VTermManager::TerminalCell>& VTermManager::get_screen_row(int row) {
    // In alternate screen (vi mode), ignore scroll offset - no scrollback
    if (!in_alternate_screen_ && current_scroll_offset_ > 0) {
        // When scrolling up (current_scroll_offset_ > 0), show history at the top.
        // History indices are absolute across the on-disk log; GetHistoryLine
        // serves them from the tail window or pages them in from the file.
        const int64_t history_lines_to_show =
            std::min<int64_t>(current_scroll_offset_, history_total_);

        // If this row is in the history section
        if (row < history_lines_to_show) {
            const int64_t history_index = history_total_ - history_lines_to_show + row;
            return GetHistoryLine(history_index);
        }
        // Otherwise get from current screen (shifted down)
        else {
            const int64_t screen_row = row - history_lines_to_show;
            if (screen_row >= 0 && screen_row < rows_ &&
                screen_row < static_cast<int64_t>(cell_buffer_.size())) {
                return cell_buffer_[static_cast<size_t>(screen_row)];
            }
        }
    }
    // Not scrolling or in alternate screen, get from current screen normally
    else if (row >= 0 && row < rows_ && row < static_cast<int>(cell_buffer_.size())) {
        return cell_buffer_[static_cast<size_t>(row)];
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
    int total_lines = static_cast<int>(history_total_) + rows_;
    int max_offset = total_lines - rows_; // Maximum scroll offset (showing oldest content)
    
    // Safety check - if total_lines is less than or equal to rows_, we cannot scroll up
    if (max_offset <= 0) {
        current_scroll_offset_ = 0;
        return;
    }
    
    int old_offset = current_scroll_offset_;
    current_scroll_offset_ = (std::min)(current_scroll_offset_ + lines, max_offset);
    
    SSH_LOG("scroll_up: lines=" << lines << ", old_offset=" << old_offset << ", new_offset=" << current_scroll_offset_ << ", max_offset=" << max_offset << ", history_size=" << history_total_);
}

void VTermManager::scroll_down(int lines) {
    int old_offset = current_scroll_offset_;
    current_scroll_offset_ = (std::max)(current_scroll_offset_ - lines, 0);
    
    SSH_LOG("scroll_down: lines=" << lines << ", old_offset=" << old_offset << ", new_offset=" << current_scroll_offset_ << ", history_size=" << history_total_);
}

void VTermManager::scroll_to_top() {
    int total_lines = static_cast<int>(history_total_) + rows_;
    current_scroll_offset_ = (std::max)(0, total_lines - rows_);
}

void VTermManager::scroll_to_bottom() {
    current_scroll_offset_ = 0;
}

bool VTermManager::can_scroll_up() const {
    int total_lines = static_cast<int>(history_total_) + rows_;
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
