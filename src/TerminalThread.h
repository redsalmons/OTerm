#pragma once

#include <wx/wx.h>
#include <wx/thread.h>
#include <uv.h>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include "SSHManager.h"
#include "VTermManager.h"
#include "ScreenBuffer.h"
#include "DeviceConfig.h"
#include "TermGLCanvas.h"
#include "EventProxy.h"

// Custom event for terminal damage notification
wxDECLARE_EVENT(wxEVT_TERMINAL_DAMAGE, wxThreadEvent);
wxDECLARE_EVENT(wxEVT_TERMINAL_EXIT, wxThreadEvent);

class TerminalDamageEvent : public wxThreadEvent {
public:
    TerminalDamageEvent(int rows, int cols, int cursor_row, int cursor_col, int damage_start_row, int damage_end_row, int damage_start_col, int damage_end_col, bool cursor_visible = true)
        : wxThreadEvent(wxEVT_TERMINAL_DAMAGE),
          m_rows(rows), m_cols(cols),
          m_cursor_row(cursor_row), m_cursor_col(cursor_col),
          m_damage_start_row(damage_start_row), m_damage_end_row(damage_end_row),
          m_damage_start_col(damage_start_col), m_damage_end_col(damage_end_col),
          m_cursor_visible(cursor_visible) {
        SetInt(rows);
    }
    
    virtual wxEvent* Clone() const override {
        return new TerminalDamageEvent(*this);
    }
    
    int GetRows() const { return m_rows; }
    int GetCols() const { return m_cols; }
    int GetCursorRow() const { return m_cursor_row; }
    int GetCursorCol() const { return m_cursor_col; }
    int GetDamageStartRow() const { return m_damage_start_row; }
    int GetDamageEndRow() const { return m_damage_end_row; }
    int GetDamageStartCol() const { return m_damage_start_col; }
    int GetDamageEndCol() const { return m_damage_end_col; }
    bool GetCursorVisible() const { return m_cursor_visible; }
    
private:
    int m_rows;
    int m_cols;
    int m_cursor_row;
    int m_cursor_col;
    int m_damage_start_row;
    int m_damage_end_row;
    int m_damage_start_col;
    int m_damage_end_col;
    bool m_cursor_visible;
};

// Worker thread for each SSH connection
// Runs its own libuv event loop and manages SSH/VTerm independently
class TerminalThread : public wxThread {
public:
    TerminalThread(EventProxyPtr event_proxy, int rows, int cols, const DeviceConfig& config);
    virtual ~TerminalThread();
    
    // Thread-safe input queue for user keystrokes
    void QueueInput(const std::string& input);
    
    // Queue SSH received data (called from libuv callback)
    void QueueSshData(const char* data, int length);
    
    // Get front buffer (read-only for UI thread)
    const ScreenBuffer* GetFrontBuffer() const { return &m_front_buffer; }
    std::string GetPassword() const { return m_deviceConfig.password; }
    DeviceConfig GetDeviceConfig() const { return m_deviceConfig; }
    
    // Start SSH connection
    void Connect();
    
    // Resize vterm (thread-safe, called from UI thread)
    void ResizeVTerm(int rows, int cols);
    
    // Scroll vterm history (thread-safe, called from UI thread)
    void ScrollVTerm(int lines);
    
    // Reset scroll to bottom (thread-safe, called from UI thread)
    void ResetScrollToBottom();
    void SetEventProxy(EventProxyPtr event_proxy);
    void SetShuttingDown() { 
        std::lock_guard<std::mutex> lock(m_shutdown_mutex);
        m_shutting_down = true; 
    }
    bool IsShuttingDown() const {
        std::lock_guard<std::mutex> lock(m_shutdown_mutex);
        return m_shutting_down;
    }
    bool IsInAlternateScreen() const { return m_vtermManager.is_in_alternate_screen(); }
    int GetScrollOffset() const { return m_vtermManager.get_scroll_offset(); }
    
protected:
    virtual ExitCode Entry() override;
    
private:
    void setup_callbacks();
    void process_input_queue();
    bool process_ssh_data_queue();
    void swap_buffers();
    void send_damage_event(bool cursor_visible = true);
    void cleanup();
    void process_resize();
    
    // Thread components
    uv_loop_t m_loop;
    SSHManager m_sshManager;
    VTermManager m_vtermManager;
    
    // Thread-safe input queue
    std::queue<std::string> m_input_queue;
    std::mutex m_input_mutex;
    std::condition_variable m_input_cv;

    // SSH received data queue (written by libuv thread, read by main loop)
    std::vector<std::string> m_ssh_data_queue;
    std::mutex m_ssh_data_mutex;
    
    // Resize request (thread-safe)
    std::pair<int, int> m_resize_request;
    bool m_resize_pending;
    std::mutex m_resize_mutex;
    
    // Shutdown flag (thread-safe)
    bool m_shutting_down;
    mutable std::mutex m_shutdown_mutex;
    
    // UI thread communication via EventProxy
    EventProxyPtr m_event_proxy;
    
    // Double buffering
    ScreenBuffer m_front_buffer;  // Read by UI thread
    ScreenBuffer m_back_buffer;   // Written by worker thread
    
    // Configuration
    DeviceConfig m_deviceConfig;
    int m_rows;
    int m_cols;
    
    // State
    bool m_has_damage;
    VTermRect m_damage_rect;
    std::chrono::steady_clock::time_point m_last_ui_update;
    bool m_heavy_streaming;
    int m_consecutive_updates;

    // Pending input buffer for interactive login prompts
    std::string m_pending_input;
};
