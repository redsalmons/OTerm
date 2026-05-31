#include "TerminalThread.h"
#include "TermGLCanvas.h"
#include <iostream>

wxDEFINE_EVENT(wxEVT_TERMINAL_DAMAGE, wxThreadEvent);
wxDEFINE_EVENT(wxEVT_TERMINAL_EXIT, wxThreadEvent);

TerminalThread::TerminalThread(wxEvtHandler* ui_handler, int rows, int cols, const DeviceConfig& config)
    : wxThread(wxTHREAD_JOINABLE),
      m_ui_handler(ui_handler),
      m_deviceConfig(config),
      m_rows(rows),
      m_cols(cols),
      m_has_damage(false),
      m_resize_pending(false) {
    m_front_buffer.resize(rows, cols);
    m_back_buffer.resize(rows, cols);
    m_damage_rect = {0, 0, 0, 0};
}

TerminalThread::~TerminalThread() {
    // Cleanup is done in Entry() when thread exits
}

void TerminalThread::QueueInput(const std::string& input) {
    std::lock_guard<std::mutex> lock(m_input_mutex);
    m_input_queue.push(input);
    m_input_cv.notify_one();
}

void TerminalThread::ResizeVTerm(int rows, int cols) {
    std::lock_guard<std::mutex> lock(m_resize_mutex);
    m_resize_request = {rows, cols};
    m_resize_pending = true;
}

void TerminalThread::ScrollVTerm(int lines) {
    // Call VTermManager scroll methods directly (thread-safe as it only reads/modifies internal state)
    if (lines > 0) {
        m_vtermManager.scroll_up(lines);
    } else {
        m_vtermManager.scroll_down(-lines);
    }
    
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
            inst.fg_color = cell.fg_color;
            inst.bg_color = cell.bg_color;
            inst.char_code = cell.char_code;
            inst.width = cell.width;
        }
    }
    
    // Update cursor position from VTerm when at bottom
    if (m_vtermManager.get_scroll_offset() == 0) {
        VTermPos cursor_pos = m_vtermManager.get_cursor_pos();
        m_back_buffer.cursor_row = cursor_pos.row;
        m_back_buffer.cursor_col = cursor_pos.col;
    }
    
    // Swap buffers so front buffer contains the updated data
    swap_buffers();
    
    // Show cursor only when at bottom (scroll offset = 0)
    bool cursor_visible = (m_vtermManager.get_scroll_offset() == 0);
    
    // Trigger damage event to refresh display
    send_damage_event(cursor_visible);
}

void TerminalThread::ResetScrollToBottom() {
    // Always reset scroll offset to 0 to show current screen (latest content)
    m_vtermManager.set_scroll_offset(0);
    
    // Manually update back buffer with get_screen_row (will show current screen)
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
    
    // Update cursor position from VTerm
    VTermPos cursor_pos = m_vtermManager.get_cursor_pos();
    m_back_buffer.cursor_row = cursor_pos.row;
    m_back_buffer.cursor_col = cursor_pos.col;
    SSH_LOG("ResetScrollToBottom: cursor updated to " << cursor_pos.row << "," << cursor_pos.col);
    
    SSH_LOG("ResetScrollToBottom: back buffer updated");
    
    // Swap buffers so front buffer contains the updated data
    swap_buffers();
    
    // Trigger damage event to refresh display (cursor visible at bottom)
    send_damage_event(true);
}

void TerminalThread::Connect() {
    // Connection will be started in Entry() thread
}

wxThread::ExitCode TerminalThread::Entry() {
    std::cout << "TerminalThread started" << std::endl;
    
    // Initialize libuv loop
    if (uv_loop_init(&m_loop) != 0) {
        std::cerr << "Failed to initialize libuv loop" << std::endl;
        return (ExitCode)1;
    }
    
    // Initialize SSH Manager
    if (!m_sshManager.initialize(&m_loop)) {
        std::cerr << "Failed to initialize SSH Manager" << std::endl;
        uv_loop_close(&m_loop);
        return (ExitCode)1;
    }
    
    // Initialize VTerm Manager
    if (!m_vtermManager.initialize(m_rows, m_cols)) {
        std::cerr << "Failed to initialize VTerm Manager" << std::endl;
        m_sshManager.cleanup();
        uv_loop_close(&m_loop);
        return (ExitCode)1;
    }
    
    // Setup callbacks
    setup_callbacks();

    // Connect to SSH
    if (m_deviceConfig.port.empty()) {
        SSH_LOG("TerminalThread: Port is empty, cannot connect");
        return (ExitCode)1;
    }
    m_sshManager.connect(m_deviceConfig.address, std::stoi(m_deviceConfig.port),
                         m_deviceConfig.username, m_deviceConfig.password);
    
    // Main loop
    while (!TestDestroy()) {
        // Run libuv event loop once
        uv_run(&m_loop, UV_RUN_ONCE);
        
        // Process input queue
        process_input_queue();
        
        // Process resize request
        {
            std::lock_guard<std::mutex> lock(m_resize_mutex);
            if (m_resize_pending) {
                int new_rows = m_resize_request.first;
                int new_cols = m_resize_request.second;
                m_resize_pending = false;
                
                // Update VTerm size
                m_vtermManager.resize(new_rows, new_cols);
                
                // Update buffer sizes
                m_front_buffer.resize(new_rows, new_cols);
                m_back_buffer.resize(new_rows, new_cols);
                
                // Clear both buffers to remove stale data
                m_front_buffer.clear();
                m_back_buffer.clear();
                
                // Update SSH terminal size
                if (m_sshManager.is_ready()) {
                    SSH_LOG("TerminalThread: Calling resize_terminal with " << new_rows << "x" << new_cols);
                    m_sshManager.resize_terminal(new_rows, new_cols);
                } else {
                    SSH_LOG("TerminalThread: SSH not ready, skipping resize_terminal");
                }
                
                m_rows = new_rows;
                m_cols = new_cols;
                
                // Log window resize information
                int vterm_rows = m_vtermManager.get_rows();
                int history_size = m_vtermManager.get_scroll_history().size();
                SSH_LOG("Window resize: new window size=" << new_rows << "x" << new_cols << ", VTerm rows=" << vterm_rows << ", history size=" << history_size);
                
                // Reset scroll offset to 0 on resize to show latest content at bottom
                m_vtermManager.set_scroll_offset(0);
                
                // Update cursor position to ensure it's within new window bounds
                VTermPos cursor_pos = m_vtermManager.get_cursor_pos();
                if (cursor_pos.row >= new_rows) {
                    // Keep cursor at current position (don't adjust)
                    SSH_LOG("Resize: cursor row " << cursor_pos.row << " exceeds new_rows " << new_rows << ", keeping as is");
                }
                if (cursor_pos.col >= new_cols) {
                    cursor_pos.col = new_cols - 1;
                    SSH_LOG("Resize: adjusted cursor col from " << m_vtermManager.get_cursor_pos().col << " to " << cursor_pos.col);
                }
                m_back_buffer.cursor_row = cursor_pos.row;
                m_back_buffer.cursor_col = cursor_pos.col;
                
                // Update back buffer with new size
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
                    }
                }
                
                // Mark as damaged to trigger refresh
                m_has_damage = true;
                
                std::cout << "TerminalThread: Resized to " << new_rows << "x" << new_cols << std::endl;
            }
        }
        
        // Check if we have damage to report
        if (m_has_damage) {
            swap_buffers();
            send_damage_event();
            m_has_damage = false;
        }
        
        // Small sleep to prevent CPU spinning
        wxMilliSleep(1);
    }
    
    // Cleanup
    cleanup();
    
    std::cout << "TerminalThread exiting" << std::endl;
    return (ExitCode)0;
}

void TerminalThread::setup_callbacks() {
    // SSH data callback -> feed to VTerm
    m_sshManager.set_data_callback([this](const char* data, int length) {
        m_vtermManager.write_input(data, length);
    });
    
    // SSH resize callback -> send terminal size to SSH
    m_sshManager.set_resize_callback([this](int rows, int cols) {
        if (rows == -1 && cols == -1) {
            // Initial resize request from SSH ready event
            SSH_LOG("TerminalThread: Initial resize request from SSH ready, sending terminal size: " << m_rows << "x" << m_cols);
            m_sshManager.resize_terminal(m_rows, m_cols);
        }
    });
    
    // VTerm damage callback -> update back buffer
    m_vtermManager.set_damage_callback([this](VTermRect rect, const std::vector<std::vector<VTermManager::TerminalCell>>& cells) {
        // 使用 get_screen_row 获取每一行数据（支持从历史记录读取）
        // Use current VTerm rows instead of m_rows to avoid reading out of bounds during resize
        int vterm_rows = m_vtermManager.get_rows();
        for (int row = 0; row < vterm_rows; ++row) {
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
        // Update cursor position from VTerm AFTER screen content
        VTermPos cursor_pos = m_vtermManager.get_cursor_pos();
        m_back_buffer.cursor_row = cursor_pos.row;
        m_back_buffer.cursor_col = cursor_pos.col;
        
        // Store damage rect for partial refresh
        m_damage_rect = rect;
        m_has_damage = true;
    });
    
    // VTerm cursor callback -> update cursor position
    // Note: VTermManager doesn't have set_cursor_callback, cursor position is obtained from get_cursor_pos()
    // We'll update cursor position in damage callback instead
}

void TerminalThread::process_input_queue() {
    std::unique_lock<std::mutex> lock(m_input_mutex);
    while (!m_input_queue.empty()) {
        std::string input = m_input_queue.front();
        m_input_queue.pop();
        lock.unlock();
        
        // Send to SSH
        m_sshManager.send_data(input.c_str(), input.length());
        
        lock.lock();
    }
}

void TerminalThread::swap_buffers() {
    // Swap front and back buffers
    std::swap(m_front_buffer, m_back_buffer);
}

void TerminalThread::send_damage_event(bool cursor_visible) {
    SSH_LOG("TerminalThread::send_damage_event called");
    if (m_ui_handler) {
        TerminalDamageEvent* evt = new TerminalDamageEvent(
            m_front_buffer.rows,
            m_front_buffer.cols,
            m_front_buffer.cursor_row,
            m_front_buffer.cursor_col,
            m_damage_rect.start_row,
            m_damage_rect.end_row,
            m_damage_rect.start_col,
            m_damage_rect.end_col,
            cursor_visible
        );
        SSH_LOG("TerminalThread::send_damage_event - queuing event, rows: " << m_front_buffer.rows << ", cols: " << m_front_buffer.cols);
        wxQueueEvent(m_ui_handler, evt);
        SSH_LOG("TerminalThread::send_damage_event - event queued");
    } else {
        SSH_LOG("TerminalThread::send_damage_event - m_ui_handler is null");
    }
}

void TerminalThread::cleanup() {
    // Notify UI thread that thread is exiting (before cleanup)
    if (m_ui_handler) {
        wxQueueEvent(m_ui_handler, new wxThreadEvent(wxEVT_TERMINAL_EXIT));
    }
    
    // Cleanup SSH Manager
    m_sshManager.cleanup();
    
    // Cleanup VTerm
    m_vtermManager.cleanup();
    
    // Close libuv loop
    while (uv_run(&m_loop, UV_RUN_NOWAIT) != 0) {
    }
    uv_loop_close(&m_loop);
}
