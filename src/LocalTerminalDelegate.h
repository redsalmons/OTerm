#ifndef LOCAL_TERMINAL_DELEGATE_H
#define LOCAL_TERMINAL_DELEGATE_H

#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include "LocalTerminalManager.h"
#include "VTermManager.h"
#include "ScreenBuffer.h"

// Delegate class to manage terminal data and state
// Lifecycle is managed by LocalTerminalContainer, not by the thread
class LocalTerminalDelegate {
public:
    LocalTerminalDelegate(int rows, int cols, const std::string& shell);
    ~LocalTerminalDelegate();
    
    // Thread-safe input queue
    void QueueInput(const std::string& input);
    
    // Get front buffer (read-only for UI thread)
    const ScreenBuffer* GetFrontBuffer() const { return &m_front_buffer; }
    
    // Start local terminal
    bool Start();
    
    // Resize vterm (thread-safe, called from UI thread)
    void ResizeVTerm(int rows, int cols);
    
    // Scroll vterm history (thread-safe, called from UI thread)
    void ScrollVTerm(int lines);
    
    // Reset scroll to bottom (thread-safe, called from UI thread)
    void ResetScrollToBottom();
    
    // Read from terminal
    int Read(char* buffer, size_t size);
    
    // Write to VTerm
    void WriteToVTerm(const char* data, size_t length);
    
    // Process input queue
    void ProcessInputQueue();
    
    // Process resize requests
    void ProcessResize();
    
    // Check for damage
    bool HasDamage() const;
    
    // Swap buffers
    void SwapBuffers();
    
    // Get damage rect
    void GetDamageRect(int& start_row, int& end_row, int& start_col, int& end_col);
    
    // Clear damage flag
    void ClearDamage();
    
    // Setup VTerm callbacks
    void SetupVTermCallbacks();
    
    // Cleanup
    void Cleanup();
    
    // UI handler
    void SetUIHandler(void* handler) { m_ui_handler = handler; }
    void* GetUIHandler() const { return m_ui_handler; }
    
    // Shutdown
    void SetShuttingDown(bool shutting_down) { 
        std::lock_guard<std::mutex> lock(m_shutdown_mutex);
        m_shutting_down = shutting_down; 
    }
    bool IsShuttingDown() const {
        std::lock_guard<std::mutex> lock(m_shutdown_mutex);
        return m_shutting_down;
    }
    
    // Pause/Resume
    void Pause();
    void Resume();
    bool IsPaused() const;
    
    // State
    bool IsInAlternateScreen() const { return m_vtermManager.is_in_alternate_screen(); }
    int GetScrollOffset() const { return m_vtermManager.get_scroll_offset(); }

private:
    // Terminal components
    LocalTerminalManager m_terminalManager;
    VTermManager m_vtermManager;
    
    // Thread-safe input queue
    std::queue<std::string> m_input_queue;
    mutable std::mutex m_input_mutex;
    std::condition_variable m_input_cv;
    
    // Resize request (thread-safe)
    std::pair<int, int> m_resize_request;
    bool m_resize_pending;
    std::mutex m_resize_mutex;
    
    // Shutdown flag (thread-safe)
    bool m_shutting_down;
    mutable std::mutex m_shutdown_mutex;
    
    // Pause/Resume mechanism
    bool m_paused;
    mutable std::mutex m_pause_mutex;
    std::condition_variable m_pause_condition;
    
    // UI thread communication
    void* m_ui_handler;
    
    // Double buffering
    ScreenBuffer m_front_buffer;  // Read by UI thread
    ScreenBuffer m_back_buffer;   // Written by worker thread
    
    // Configuration
    std::string m_shell;
    int m_rows;
    int m_cols;
    
    // State
    bool m_has_damage;
    int m_damage_start_row;
    int m_damage_end_row;
    int m_damage_start_col;
    int m_damage_end_col;
};

#endif
