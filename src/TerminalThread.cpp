#include "TerminalThread.h"
#include "TermGLCanvas.h"
#include <iostream>

wxDEFINE_EVENT(wxEVT_TERMINAL_DAMAGE, wxThreadEvent);
wxDEFINE_EVENT(wxEVT_TERMINAL_EXIT, wxThreadEvent);

TerminalThread::TerminalThread(EventProxyPtr event_proxy, int rows, int cols, const DeviceConfig& config)
    : wxThread(wxTHREAD_JOINABLE),
      m_event_proxy(event_proxy),
      m_deviceConfig(config),
      m_rows(rows),
      m_cols(cols),
      m_has_damage(false),
      m_shutting_down(false),
      m_resize_pending(false),
      m_heavy_streaming(false),
      m_consecutive_updates(0) {
    m_front_buffer.resize(rows, cols);
    m_back_buffer.resize(rows, cols);
    m_damage_rect = {0, 0, 0, 0};
    m_last_ui_update = std::chrono::steady_clock::now();
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

void TerminalThread::SetEventProxy(EventProxyPtr event_proxy) {
    std::lock_guard<std::mutex> lock(m_input_mutex);
    m_event_proxy = event_proxy;
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
                         m_deviceConfig.username, m_deviceConfig.password,
                         m_deviceConfig.auth_method);
    
    // Main loop
    while (!TestDestroy() && !IsShuttingDown()) {
        // Run libuv event loop - non-blocking, returns immediately if no events
        uv_run(&m_loop, UV_RUN_NOWAIT);
        
        // Batch process all queued SSH data, flush damage only once
        bool had_data = process_ssh_data_queue();
        
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
        
        // Stream state detection: enter heavy streaming if we receive data consecutively
        if (had_data) {
            m_consecutive_updates++;
            if (m_consecutive_updates >= 5) {
                m_heavy_streaming = true;
            }
        } else {
            m_consecutive_updates = 0;
            m_heavy_streaming = false; // Immediately exit heavy streaming when idle
        }
        
        // Check if we have damage to report
        if (m_has_damage) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_last_ui_update).count();
            
            // Dynamic refresh rate based on stream speed:
            // 1. Heavy streaming: Throttle to ~150ms (Approx 6 FPS) so scrolling is super lightweight
            // 2. Regular / typing: Throttle to ~16ms (60 FPS) for smooth interactive input
            // 3. Just went idle (!had_data): Render immediately with 0ms delay for the final frame
            int throttle_ms = m_heavy_streaming ? 150 : 16;
            
            if (!had_data || elapsed >= throttle_ms) {
                // Copy full screen from VTerm cell buffer to back buffer once
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
                VTermPos cursor_pos = m_vtermManager.get_cursor_pos();
                m_back_buffer.cursor_row = cursor_pos.row;
                m_back_buffer.cursor_col = cursor_pos.col;
                m_damage_rect = {0, 0, vterm_rows, m_cols};
                m_has_damage = false;
                swap_buffers();
                send_damage_event();
                m_last_ui_update = now;
            }
        }
        
        // Only sleep when no data was processed to avoid CPU spinning
        if (!had_data) {
            wxMilliSleep(1);
        }
    }
    
    // Cleanup
    cleanup();
    
    std::cout << "TerminalThread exiting" << std::endl;
    return (ExitCode)0;
}

void TerminalThread::QueueSshData(const char* data, int length) {
    std::lock_guard<std::mutex> lock(m_ssh_data_mutex);
    m_ssh_data_queue.emplace_back(data, length);
}

bool TerminalThread::process_ssh_data_queue() {
    std::vector<std::string> batch;
    {
        std::lock_guard<std::mutex> lock(m_ssh_data_mutex);
        if (m_ssh_data_queue.empty()) return false;
        batch.swap(m_ssh_data_queue);
    }
    
    auto t0 = std::chrono::steady_clock::now();
    size_t batch_bytes = 0;
    // Write all batched data to vterm without flushing damage in between
    for (const auto& chunk : batch) {
        batch_bytes += chunk.size();
        m_vtermManager.write_input_no_flush(chunk.c_str(), chunk.size());
    }
    // Flush damage once after all data is written
    m_vtermManager.flush_damage();
    
    auto t1 = std::chrono::steady_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    
    static size_t total_bytes = 0;
    static auto last_log = std::chrono::steady_clock::now();
    total_bytes += batch_bytes;
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(t1 - last_log).count();
    if (elapsed >= 1) {
        SSH_LOG("THROUGHPUT: " << (total_bytes / 1024) << " KB/s, batch processed: " << batch_bytes << " bytes, vterm write took: " << us << "us");
        total_bytes = 0;
        last_log = t1;
    }
    
    return true;
}

void TerminalThread::setup_callbacks() {
    // SSH data callback -> queue data for batch processing in main loop
    m_sshManager.set_data_callback([this](const char* data, int length) {
        QueueSshData(data, length);
    });
    
    // SSH status callback -> queue data for batch processing in main loop
    m_sshManager.set_status_callback([this](const char* data, int length) {
        QueueSshData(data, length);
    });

    // SSH resize callback -> send terminal size to SSH
    m_sshManager.set_resize_callback([this](int rows, int cols) {
        if (rows == -1 && cols == -1) {
            // Initial resize request from SSH ready event
            SSH_LOG("TerminalThread: Initial resize request from SSH ready, sending terminal size: " << m_rows << "x" << m_cols);
            m_sshManager.resize_terminal(m_rows, m_cols);
        }
    });
    
    // VTerm damage callback -> just mark dirty, actual copy is done once in main loop
    m_vtermManager.set_damage_callback([this](VTermRect rect, const std::vector<std::vector<VTermManager::TerminalCell>>& cells) {
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

        SSHManager::SSHState state = m_sshManager.get_state();

        if (state == SSHManager::SSH_PROMPTING_USERNAME) {
            for (char c : input) {
                if (c == '\r' || c == '\n') {
                    m_vtermManager.write_input("\r\n", 2);
                    m_sshManager.provide_username(m_pending_input);
                    m_pending_input.clear();
                } else if (c == '\x7f' || c == '\x08') {
                    if (!m_pending_input.empty()) {
                        m_pending_input.pop_back();
                        const char* bs = "\x08 \x08";
                        m_vtermManager.write_input(bs, 3);
                    }
                } else {
                    m_pending_input += c;
                    m_vtermManager.write_input(&c, 1);
                }
            }
        } else if (state == SSHManager::SSH_PROMPTING_PASSWORD) {
            for (char c : input) {
                if (c == '\r' || c == '\n') {
                    m_vtermManager.write_input("\r\n", 2);
                    m_sshManager.provide_password(m_pending_input);
                    m_pending_input.clear();
                } else if (c == '\x7f' || c == '\x08') {
                    if (!m_pending_input.empty()) {
                        m_pending_input.pop_back();
                    }
                } else {
                    m_pending_input += c;
                }
            }
        } else {
            m_sshManager.send_data(input.c_str(), input.length());
        }

        lock.lock();
    }
}

void TerminalThread::swap_buffers() {
    // Update cursor position to the final, settled position from VTerm before swapping
    VTermPos cursor_pos = m_vtermManager.get_cursor_pos();
    m_back_buffer.cursor_row = cursor_pos.row;
    m_back_buffer.cursor_col = cursor_pos.col;

    // Swap front and back buffers
    std::swap(m_front_buffer, m_back_buffer);
}

void TerminalThread::send_damage_event(bool cursor_visible) {
    // SSH_LOG("TerminalThread::send_damage_event called");
    if (m_event_proxy) {
        // Use EventProxy to post damage event
        m_event_proxy->PostDamageEvent(m_front_buffer.rows, m_front_buffer.cols, 
                                        m_front_buffer.cursor_row, m_front_buffer.cursor_col, 0);
    } else {
        SSH_LOG("TerminalThread::send_damage_event - m_event_proxy is null");
    }
}

void TerminalThread::cleanup() {
    // No need to notify UI thread via EventProxy for exit
    // The UI will detect thread termination through other means
    
    // Cleanup SSH Manager
    m_sshManager.cleanup();
    
    // Cleanup VTerm
    m_vtermManager.cleanup();
    
    // Close libuv loop
    while (uv_run(&m_loop, UV_RUN_NOWAIT) != 0) {
    }
    uv_loop_close(&m_loop);
}
