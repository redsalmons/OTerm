#pragma once

#include <wx/wx.h>
#include <wx/thread.h>
#include <queue>
#include <mutex>
#include <condition_variable>
#include "LocalTerminalManager.h"
#include "VTermManager.h"
#include "ScreenBuffer.h"
#include "TermGLCanvas.h"

// Worker thread for local terminal
class LocalTerminalThread : public wxThread {
public:
    LocalTerminalThread(wxEvtHandler* ui_handler, int rows, int cols, const std::string& shell = "");
    virtual ~LocalTerminalThread();
    
    // Thread-safe input queue for user keystrokes
    void QueueInput(const std::string& input);
    
    // Get front buffer (read-only for UI thread)
    const ScreenBuffer* GetFrontBuffer() const { return &m_front_buffer; }
    
    // Start local terminal
    void Start();
    
    // Resize vterm (thread-safe, called from UI thread)
    void ResizeVTerm(int rows, int cols);
    
    // Scroll vterm history (thread-safe, called from UI thread)
    void ScrollVTerm(int lines);
    
    // Reset scroll to bottom (thread-safe, called from UI thread)
    void ResetScrollToBottom();
    void ClearUIHandler();
    void SetShuttingDown() { 
        std::lock_guard<std::mutex> lock(m_shutdown_mutex);
        m_shutting_down = true; 
    }
    bool IsInAlternateScreen() const { return m_vtermManager.is_in_alternate_screen(); }
    int GetScrollOffset() const { return m_vtermManager.get_scroll_offset(); }
    
protected:
    virtual ExitCode Entry() override;
    
private:
    void setup_callbacks();
    void process_input_queue();
    void swap_buffers();
    void send_damage_event(bool cursor_visible = true);
    void cleanup();
    void process_resize();
    
    // Thread components
    LocalTerminalManager m_terminalManager;
    VTermManager m_vtermManager;
    
    // Thread-safe input queue
    std::queue<std::string> m_input_queue;
    std::mutex m_input_mutex;
    std::condition_variable m_input_cv;
    
    // Resize request (thread-safe)
    std::pair<int, int> m_resize_request;
    bool m_resize_pending;
    std::mutex m_resize_mutex;
    
    // Shutdown flag (thread-safe)
    bool m_shutting_down;
    std::mutex m_shutdown_mutex;
    
    // UI thread communication
    wxEvtHandler* m_ui_handler;
    
    // Double buffering
    ScreenBuffer m_front_buffer;  // Read by UI thread
    ScreenBuffer m_back_buffer;   // Written by worker thread
    
    // Configuration
    std::string m_shell;
    int m_rows;
    int m_cols;
    
    // State
    bool m_has_damage;
    VTermRect m_damage_rect;
};
