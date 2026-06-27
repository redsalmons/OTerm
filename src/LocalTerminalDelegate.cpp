#include "LocalTerminalDelegate.h"
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

static void DELEGATE_LOG(const std::string& msg) {
    std::ofstream f((std::filesystem::temp_directory_path() / "oterm_alert.log").string(), std::ios::app);
    if (f.is_open()) {
        f << "[DELEGATE] " << msg << std::endl;
        f.flush();
    }
}

LocalTerminalDelegate::LocalTerminalDelegate(int rows, int cols, const std::string& shell)
    : m_vtermManager(),
      m_ui_handler(nullptr),
      m_shell(shell),
      m_rows(rows),
      m_cols(cols),
      m_resize_pending(false),
      m_shutting_down(false),
      m_paused(false),
      m_pause_condition(),
      m_has_damage(false),
      m_damage_start_row(0),
      m_damage_end_row(rows),
      m_damage_start_col(0),
      m_damage_end_col(cols) {
    DELEGATE_LOG("LocalTerminalDelegate constructor called");
    m_front_buffer.resize(rows, cols);
    m_back_buffer.resize(rows, cols);
    m_vtermManager.initialize(rows, cols);
}

LocalTerminalDelegate::~LocalTerminalDelegate() {
    DELEGATE_LOG("LocalTerminalDelegate destructor called");
    Cleanup();
}

bool LocalTerminalDelegate::Start() {
    DELEGATE_LOG("Starting terminal");
    bool result = m_terminalManager.Start(m_shell);
    if (result) {
        DELEGATE_LOG("Terminal started successfully");
        SetupVTermCallbacks();
    } else {
        DELEGATE_LOG("Terminal start failed");
    }
    return result;
}

void LocalTerminalDelegate::QueueInput(const std::string& input) {
    std::lock_guard<std::mutex> lock(m_input_mutex);
    m_input_queue.push(input);
    m_input_cv.notify_one();
}

void LocalTerminalDelegate::ResizeVTerm(int rows, int cols) {
    std::lock_guard<std::mutex> lock(m_resize_mutex);
    m_resize_request = std::make_pair(rows, cols);
    m_resize_pending = true;
}

void LocalTerminalDelegate::ScrollVTerm(int lines) {
    if (lines > 0) {
        m_vtermManager.scroll_up(lines);
    } else {
        m_vtermManager.scroll_down(-lines);
    }
}

void LocalTerminalDelegate::ResetScrollToBottom() {
    m_vtermManager.scroll_to_bottom();
}

int LocalTerminalDelegate::Read(char* buffer, size_t size) {
    return m_terminalManager.Read(buffer, size);
}

void LocalTerminalDelegate::WriteToVTerm(const char* data, size_t length) {
    m_vtermManager.write_input(data, length);
}

void LocalTerminalDelegate::ProcessInputQueue() {
    std::unique_lock<std::mutex> lock(m_input_mutex);
    while (!m_input_queue.empty()) {
        std::string input = m_input_queue.front();
        m_input_queue.pop();
        lock.unlock();
        
        m_terminalManager.Write(input.c_str(), input.length());
        
        lock.lock();
    }
}

void LocalTerminalDelegate::ProcessResize() {
    std::lock_guard<std::mutex> lock(m_resize_mutex);
    if (m_resize_pending) {
        m_vtermManager.resize(m_resize_request.first, m_resize_request.second);
        m_front_buffer.resize(m_resize_request.first, m_resize_request.second);
        m_back_buffer.resize(m_resize_request.first, m_resize_request.second);
        m_rows = m_resize_request.first;
        m_cols = m_resize_request.second;
        m_resize_pending = false;
    }
}

bool LocalTerminalDelegate::HasDamage() const {
    std::lock_guard<std::mutex> lock(m_input_mutex);
    return m_has_damage;
}

void LocalTerminalDelegate::SwapBuffers() {
    std::lock_guard<std::mutex> lock(m_input_mutex);
    std::swap(m_front_buffer, m_back_buffer);
}

void LocalTerminalDelegate::GetDamageRect(int& start_row, int& end_row, int& start_col, int& end_col) {
    std::lock_guard<std::mutex> lock(m_input_mutex);
    start_row = m_damage_start_row;
    end_row = m_damage_end_row;
    start_col = m_damage_start_col;
    end_col = m_damage_end_col;
}

void LocalTerminalDelegate::ClearDamage() {
    std::lock_guard<std::mutex> lock(m_input_mutex);
    m_has_damage = false;
}

void LocalTerminalDelegate::SetupVTermCallbacks() {
    // Setup VTerm damage callback
    m_vtermManager.set_damage_callback([this](VTermRect rect, const std::vector<std::vector<VTermManager::TerminalCell>>& cells) {
        std::lock_guard<std::mutex> lock(m_input_mutex);
        m_has_damage = true;
        m_damage_start_row = rect.start_row;
        m_damage_end_row = rect.end_row;
        m_damage_start_col = rect.start_col;
        m_damage_end_col = rect.end_col;
    });
    
    // Setup other callbacks as needed
}

void LocalTerminalDelegate::Cleanup() {
    DELEGATE_LOG("Cleanup called");
    // Cleanup terminal manager
    m_terminalManager.Stop();
}

void LocalTerminalDelegate::Pause() {
    std::lock_guard<std::mutex> lock(m_pause_mutex);
    m_paused = true;
    DELEGATE_LOG("Delegate paused");
}

void LocalTerminalDelegate::Resume() {
    std::lock_guard<std::mutex> lock(m_pause_mutex);
    m_paused = false;
    m_pause_condition.notify_all();
    DELEGATE_LOG("Delegate resumed");
}

bool LocalTerminalDelegate::IsPaused() const {
    std::lock_guard<std::mutex> lock(m_pause_mutex);
    return m_paused;
}
