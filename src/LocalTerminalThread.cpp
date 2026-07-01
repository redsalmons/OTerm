#include "LocalTerminalThread.h"
#include "TerminalThread.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <filesystem>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

static void LT_LOG(const std::string& msg) {
    std::ofstream f((std::filesystem::temp_directory_path() / "oterm_alert.log").string(), std::ios::app);
    if (f.is_open()) {
        f << "[LT-DEBUG] " << msg << std::endl;
        f.flush();
    }
}

LocalTerminalThread::LocalTerminalThread(EventProxyPtr event_proxy, int rows, int cols, const std::string& shell)
    : wxThread(wxTHREAD_JOINABLE),
      m_vtermManager(),
      m_event_proxy(event_proxy),
      m_shell(shell),
      m_rows(rows),
      m_cols(cols),
      m_resize_pending(false),
      m_shutting_down(false),
      m_has_damage(false) {
    LT_LOG("LocalTerminalThread constructor called");
    m_front_buffer.resize(rows, cols);
    m_back_buffer.resize(rows, cols);
    m_vtermManager.initialize(rows, cols);
    m_damage_rect.start_row = 0;
    m_damage_rect.end_row = rows;
    m_damage_rect.start_col = 0;
    m_damage_rect.end_col = cols;
}

LocalTerminalThread::~LocalTerminalThread() {
    // Cleanup should be done by the owner (TermGLCanvas) using SetShuttingDown() + Wait() + delete
}

void LocalTerminalThread::Start() {
    wxThreadError err = Run();
    if (err != wxTHREAD_NO_ERROR) {
        LT_LOG("LocalTerminalThread::Start() Run() failed, err=" + std::to_string(err));
    } else {
        LT_LOG("LocalTerminalThread::Start() thread started successfully");
    }
}

void LocalTerminalThread::QueueInput(const std::string& input) {
    if (input.empty()) {
        return;
    }
    LT_LOG("QueueInput: len=" + std::to_string(input.length()) + " first=" + std::to_string((int)(unsigned char)input[0]));
    {
        std::lock_guard<std::mutex> lock(m_input_mutex);
        m_input_queue.push(input);
    }
    m_input_cv.notify_one();
}

void LocalTerminalThread::ResizeVTerm(int rows, int cols) {
    {
        std::lock_guard<std::mutex> lock(m_resize_mutex);
        m_resize_request = std::make_pair(rows, cols);
        m_resize_pending = true;
    }
}

void LocalTerminalThread::ScrollVTerm(int lines) {
    SSH_LOG("LocalTerminalThread::ScrollVTerm called with lines=" << lines);
    SSH_LOG("  Current scroll offset: " << m_vtermManager.get_scroll_offset());

    // Call VTermManager scroll methods directly (thread-safe as it only reads/modifies internal state)
    if (lines > 0) {
        m_vtermManager.scroll_up(lines);
    } else {
        m_vtermManager.scroll_down(-lines);
    }

    SSH_LOG("  After scroll, new offset: " << m_vtermManager.get_scroll_offset());

    // Manually update back buffer with get_screen_row (supports scrollback history)
    for (int row = 0; row < m_rows; ++row) {
        const auto& row_cells = m_vtermManager.get_screen_row(row);
        for (int col = 0; col < m_cols && col < (int)row_cells.size(); ++col) {
            const auto& cell = row_cells[col];
            CellInstance& inst = m_back_buffer.cells[row][col];
            inst.cell_x = (float)col;
            inst.cell_y = (float)row;
            inst.uv_u = 0;
            inst.uv_v = 0;
            inst.uv_w = 0;
            inst.uv_h = 0;
            inst.char_code = cell.char_code;
            inst.fg_color = cell.fg_color;
            inst.bg_color = cell.bg_color;
        }
    }

    // Update cursor position in back buffer
    if (m_vtermManager.get_scroll_offset() == 0) {
        VTermPos cursor_pos = m_vtermManager.get_cursor_pos();
        m_back_buffer.cursor_row = cursor_pos.row;
        m_back_buffer.cursor_col = cursor_pos.col;
    }

    SSH_LOG("  Cursor position: " << m_back_buffer.cursor_row << "," << m_back_buffer.cursor_col);
    // SSH_LOG("  Sending damage event");

    bool cursor_visible = (m_vtermManager.get_scroll_offset() == 0);
    send_damage_event(cursor_visible);
}

void LocalTerminalThread::ResetScrollToBottom() {
    m_vtermManager.scroll_to_bottom();
    send_damage_event(false);
}

void LocalTerminalThread::SetEventProxy(EventProxyPtr event_proxy) {
    std::lock_guard<std::mutex> lock(m_input_mutex);
    m_event_proxy = event_proxy;
}

void LocalTerminalThread::setup_callbacks() {
    m_vtermManager.set_damage_callback([this](VTermRect rect, const std::vector<std::vector<VTermManager::TerminalCell>>& cells) {
        std::lock_guard<std::mutex> lock(m_input_mutex);
        if (!m_has_damage) {
            m_damage_rect = rect;
            m_has_damage = true;
        } else {
            m_damage_rect.start_row = std::min(m_damage_rect.start_row, rect.start_row);
            m_damage_rect.end_row = std::max(m_damage_rect.end_row, rect.end_row);
            m_damage_rect.start_col = std::min(m_damage_rect.start_col, rect.start_col);
            m_damage_rect.end_col = std::max(m_damage_rect.end_col, rect.end_col);
        }
    });
}

void LocalTerminalThread::process_input_queue() {
    std::unique_lock<std::mutex> lock(m_input_mutex);
    while (!m_input_queue.empty()) {
        std::string input = m_input_queue.front();
        m_input_queue.pop();
        lock.unlock();

        LT_LOG("process_input_queue: writing len=" + std::to_string(input.size()));

        // Send input to local terminal
        m_terminalManager.Write(input.c_str(), input.size());

        lock.lock();
    }
}

void LocalTerminalThread::swap_buffers() {
    // Copy from VTermManager cell buffer to back buffer
    const auto& cell_buffer = m_vtermManager.get_cell_buffer();
    char32_t first_non_empty = 0;
    for (int row = 0; row < m_rows && row < (int)cell_buffer.size(); ++row) {
        for (int col = 0; col < m_cols && col < (int)cell_buffer[row].size(); ++col) {
            const auto& cell = cell_buffer[row][col];
            CellInstance& inst = m_back_buffer.cells[row][col];
            inst.char_code = cell.char_code;
            inst.fg_color = cell.fg_color;
            inst.bg_color = cell.bg_color;
            inst.width = cell.width;
            if (cell.char_code != 0 && cell.char_code != ' ' && first_non_empty == 0) {
                first_non_empty = cell.char_code;
            }
        }
    }

    // Update cursor position from VTerm
    VTermPos cursor_pos = m_vtermManager.get_cursor_pos();
    m_back_buffer.cursor_row = cursor_pos.row;
    m_back_buffer.cursor_col = cursor_pos.col;

    LT_LOG("swap_buffers: first_nonempty=" + std::to_string((int)first_non_empty) +
           " cursor=" + std::to_string(cursor_pos.row) + "," + std::to_string(cursor_pos.col));

    std::swap(m_front_buffer, m_back_buffer);
}

void LocalTerminalThread::send_damage_event(bool cursor_visible) {
    if (!m_event_proxy) {
        LT_LOG("send_damage_event: no event_proxy");
        return;
    }

    VTermPos cursor = m_vtermManager.get_cursor_pos();
    LT_LOG("send_damage_event: posting event rows=" + std::to_string(m_rows) + " cols=" + std::to_string(m_cols) +
           " cursor=" + std::to_string(cursor.row) + "," + std::to_string(cursor.col));
    
    // Use EventProxy to post damage event
    m_event_proxy->PostDamageEvent(m_rows, m_cols, cursor.row, cursor.col, 0);
}

void LocalTerminalThread::cleanup() {
    m_terminalManager.Stop();
}

void LocalTerminalThread::process_resize() {
    std::lock_guard<std::mutex> lock(m_resize_mutex);
    if (m_resize_pending) {
        int old_rows = m_rows;
        int old_cols = m_cols;
        m_rows = m_resize_request.first;
        m_cols = m_resize_request.second;

        LT_LOG("process_resize: " + std::to_string(old_rows) + "x" + std::to_string(old_cols) + 
               " -> " + std::to_string(m_rows) + "x" + std::to_string(m_cols));

        // Resize buffers to match new terminal size
        m_front_buffer.resize(m_rows, m_cols);
        m_back_buffer.resize(m_rows, m_cols);
        
        // Clear buffers to remove stale data
        m_front_buffer.clear();
        m_back_buffer.clear();

        m_vtermManager.resize(m_rows, m_cols);
        m_terminalManager.Resize(m_rows, m_cols);
        
        // Refill back buffer from VTerm after resize
        for (int row = 0; row < m_rows; ++row) {
            const auto& row_cells = m_vtermManager.get_screen_row(row);
            for (int col = 0; col < m_cols && col < (int)row_cells.size(); ++col) {
                const auto& cell = row_cells[col];
                CellInstance& inst = m_back_buffer.cells[row][col];
                inst.cell_x = (float)col;
                inst.cell_y = (float)row;
                inst.uv_u = 0;
                inst.uv_v = 0;
                inst.uv_w = 0;
                inst.uv_h = 0;
                inst.fg_color = cell.fg_color;
                inst.bg_color = cell.bg_color;
                inst.char_code = cell.char_code;
                inst.width = cell.width;
            }
        }
        
        // Swap buffers to make resized data available
        swap_buffers();
        
        m_resize_pending = false;

        // Trigger an immediate UI update
        {
            std::lock_guard<std::mutex> input_lock(m_input_mutex);
            m_has_damage = true;
            LT_LOG("process_resize: triggered damage for immediate UI update");
        }
    }
}

wxThread::ExitCode LocalTerminalThread::Entry() {
    try {
        LT_LOG("LocalTerminalThread::Entry() started");

        // Start local terminal
        if (!m_terminalManager.Start(m_shell)) {
            LT_LOG("LocalTerminalThread::Entry() m_terminalManager.Start() failed");
            return (ExitCode)1;
        }
        LT_LOG("LocalTerminalThread::Entry() m_terminalManager.Start() succeeded");

        // Setup VTerm callbacks
        setup_callbacks();

        // Main loop
        char buffer[8192];
        while (!TestDestroy() && !IsShuttingDown()) {
            // Process resize requests
            process_resize();

            // Process input queue
            process_input_queue();

            // Read from terminal
            int bytesRead = m_terminalManager.Read(buffer, sizeof(buffer));
            if (bytesRead > 0) {
                // Feed data to VTerm
                std::string hexBytes;
                for (int i = 0; i < std::min(bytesRead, 16); ++i) {
                    char hex[4];
                    snprintf(hex, sizeof(hex), "%02x ", (unsigned char)buffer[i]);
                    hexBytes += hex;
                }
                LT_LOG("LocalTerminalThread::Entry() Read() bytesRead=" + std::to_string(bytesRead) + " data=" + hexBytes);
                m_vtermManager.write_input(buffer, bytesRead);
            } else if (bytesRead < 0) {
                // Error or EOF
                LT_LOG("LocalTerminalThread::Entry() Read() returned -1, breaking loop");
                break;
            }

            // Check for damage
            {
                std::lock_guard<std::mutex> lock(m_input_mutex);
                if (m_has_damage) {
                    LT_LOG("LocalTerminalThread::Entry() has_damage, sending event");
                    swap_buffers();
                    // Show cursor only when at bottom (scroll offset = 0)
                    bool cursor_visible = (m_vtermManager.get_scroll_offset() == 0);
                    send_damage_event(cursor_visible);
                    m_has_damage = false;
                }
            }

            // Small sleep to prevent busy-waiting
#ifdef _WIN32
            Sleep(10);
#else
            usleep(10000); // 10ms
#endif
        }

        LT_LOG("LocalTerminalThread::Entry() loop ended");
        cleanup();

        return (ExitCode)0;
    } catch (const std::exception& e) {
        LT_LOG(std::string("LocalTerminalThread::Entry() EXCEPTION: ") + e.what());
    } catch (...) {
        LT_LOG("LocalTerminalThread::Entry() UNKNOWN EXCEPTION");
    }

    return (ExitCode)1;
}
