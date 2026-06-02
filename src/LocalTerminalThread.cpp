#include "LocalTerminalThread.h"
#include "TerminalThread.h"
#include <cstring>
#include <unistd.h>

LocalTerminalThread::LocalTerminalThread(wxEvtHandler* ui_handler, int rows, int cols, const std::string& shell)
    : wxThread(wxTHREAD_JOINABLE),
      m_vtermManager(),
      m_ui_handler(ui_handler),
      m_shell(shell),
      m_rows(rows),
      m_cols(cols),
      m_resize_pending(false),
      m_has_damage(false) {
    m_front_buffer.resize(rows, cols);
    m_back_buffer.resize(rows, cols);
    m_vtermManager.initialize(rows, cols);
    m_damage_rect.start_row = 0;
    m_damage_rect.end_row = rows;
    m_damage_rect.start_col = 0;
    m_damage_rect.end_col = cols;
}

LocalTerminalThread::~LocalTerminalThread() {
    if (IsRunning()) {
        Delete();
    }
}

void LocalTerminalThread::Start() {
    if (Create() == wxTHREAD_NO_ERROR) {
        Run();
    }
}

void LocalTerminalThread::QueueInput(const std::string& input) {
    SSH_LOG("LocalTerminalThread::QueueInput: input length=" << input.length() << ", content=" << std::hex << (int)(unsigned char)input[0] << " " << (int)(unsigned char)input[1] << " " << (int)(unsigned char)input[2] << std::dec);
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
    VTermPos cursor_pos = m_vtermManager.get_cursor_pos();
    m_back_buffer.cursor_row = cursor_pos.row;
    m_back_buffer.cursor_col = cursor_pos.col;

    SSH_LOG("  Cursor position: " << cursor_pos.row << "," << cursor_pos.col);
    SSH_LOG("  Sending damage event");

    send_damage_event(false);
}

void LocalTerminalThread::ResetScrollToBottom() {
    m_vtermManager.scroll_to_bottom();
    send_damage_event(false);
}

void LocalTerminalThread::ClearUIHandler() {
    std::lock_guard<std::mutex> lock(m_input_mutex);
    m_ui_handler = nullptr;
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

        SSH_LOG("LocalTerminalThread::process_input_queue: writing to terminal, length=" << input.size() << ", content=" << std::hex << (int)(unsigned char)input[0] << " " << (int)(unsigned char)input[1] << " " << (int)(unsigned char)input[2] << std::dec);

        // Send input to local terminal
        m_terminalManager.Write(input.c_str(), input.size());

        lock.lock();
    }
}

void LocalTerminalThread::swap_buffers() {
    // Copy from VTermManager cell buffer to back buffer
    const auto& cell_buffer = m_vtermManager.get_cell_buffer();
    for (int row = 0; row < m_rows && row < (int)cell_buffer.size(); ++row) {
        for (int col = 0; col < m_cols && col < (int)cell_buffer[row].size(); ++col) {
            const auto& cell = cell_buffer[row][col];
            CellInstance& inst = m_back_buffer.cells[row][col];
            inst.char_code = cell.char_code;
            inst.fg_color = cell.fg_color;
            inst.bg_color = cell.bg_color;
            inst.width = cell.width;
        }
    }

    // Update cursor position from VTerm
    VTermPos cursor_pos = m_vtermManager.get_cursor_pos();
    m_back_buffer.cursor_row = cursor_pos.row;
    m_back_buffer.cursor_col = cursor_pos.col;

    std::swap(m_front_buffer, m_back_buffer);
}

void LocalTerminalThread::send_damage_event(bool cursor_visible) {
    if (!m_ui_handler) return;

    VTermPos cursor = m_vtermManager.get_cursor_pos();
    TerminalDamageEvent event(
        m_rows, m_cols,
        cursor.row, cursor.col,
        m_damage_rect.start_row, m_damage_rect.end_row,
        m_damage_rect.start_col, m_damage_rect.end_col,
        cursor_visible
    );
    m_ui_handler->AddPendingEvent(event);
}

void LocalTerminalThread::cleanup() {
    m_terminalManager.Stop();
}

void LocalTerminalThread::process_resize() {
    std::lock_guard<std::mutex> lock(m_resize_mutex);
    if (m_resize_pending) {
        m_rows = m_resize_request.first;
        m_cols = m_resize_request.second;

        // Resize buffers to match new terminal size
        m_front_buffer.resize(m_rows, m_cols);
        m_back_buffer.resize(m_rows, m_cols);

        m_vtermManager.resize(m_rows, m_cols);
        m_terminalManager.Resize(m_rows, m_cols);
        m_resize_pending = false;
    }
}

wxThread::ExitCode LocalTerminalThread::Entry() {
    // Start local terminal
    if (!m_terminalManager.Start(m_shell)) {
        if (m_ui_handler) {
            wxThreadEvent exitEvent(wxEVT_TERMINAL_EXIT);
            m_ui_handler->AddPendingEvent(exitEvent);
        }
        return (ExitCode)1;
    }

    // Setup VTerm callbacks
    setup_callbacks();

    // Main loop
    char buffer[8192];
    while (!TestDestroy()) {
        // Process resize requests
        process_resize();

        // Process input queue
        process_input_queue();

        // Read from terminal
        int bytesRead = m_terminalManager.Read(buffer, sizeof(buffer));
        if (bytesRead > 0) {
            // Feed data to VTerm
            m_vtermManager.write_input(buffer, bytesRead);
        } else if (bytesRead < 0) {
            // Error or EOF
            break;
        }

        // Check for damage
        {
            std::lock_guard<std::mutex> lock(m_input_mutex);
            if (m_has_damage) {
                swap_buffers();
                // Show cursor only when at bottom (scroll offset = 0)
                bool cursor_visible = (m_vtermManager.get_scroll_offset() == 0);
                send_damage_event(cursor_visible);
                m_has_damage = false;
            }
        }

        // Small sleep to prevent busy-waiting
        usleep(10000); // 10ms
    }

    cleanup();

    if (m_ui_handler) {
        wxThreadEvent exitEvent(wxEVT_TERMINAL_EXIT);
        m_ui_handler->AddPendingEvent(exitEvent);
    }

    return (ExitCode)0;
}
