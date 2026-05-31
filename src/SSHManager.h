#ifndef SSHMANAGER_H
#define SSHMANAGER_H

#include <libssh2.h>
#include <uv.h>
#include <string>
#include <functional>
#include <fstream>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <filesystem>

// Logging macros for SSH operations
static std::string timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
#ifdef _WIN32
    localtime_s(&tm, &time_t);
#else
    localtime_r(&time_t, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%H:%M:%S.") << std::setw(3) << std::setfill('0')
        << (std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000);
    return oss.str();
}

extern std::ofstream ssh_log_file;
extern bool ssh_log_initialized;

#define SSH_LOG(msg) do { \
    if (ssh_log_file.is_open()) { \
        ssh_log_file << "[" << timestamp() << "] [SSH] " << msg << std::endl; \
        ssh_log_file.flush(); \
    } \
} while(0)

#define SSH_ERR(msg) do { \
    if (ssh_log_file.is_open()) { \
        ssh_log_file << "[" << timestamp() << "] [SSH] ERROR: " << msg << std::endl; \
        ssh_log_file.flush(); \
    } \
} while(0)

class SSHManager {
public:
    // SSH connection states
    enum SSHState {
        SSH_DISCONNECTED,
        SSH_CONNECTING,
        SSH_HANDSHAKING,
        SSH_AUTHENTICATING,
        SSH_CHANNEL_OPENING,
        SSH_PTY_REQUESTING,
        SSH_SHELL_REQUESTING,
        SSH_READY
    };

    // Callback type for data received from SSH
    using DataCallback = std::function<void(const char* data, int length)>;

public:
    SSHManager();
    ~SSHManager();

    // Initialize SSH manager with libuv loop
    bool initialize(uv_loop_t* loop);
    
    // Connect to SSH server
    bool connect(const std::string& host, int port, 
                const std::string& username, const std::string& password);
    
    // Send data to SSH channel
    bool send_data(const char* data, int length);
    
    // Check if SSH is ready for communication
    bool is_ready() const { return ssh_state_ == SSH_READY && ssh_channel_ != nullptr; }
    
    // Get current SSH state
    SSHState get_state() const { return ssh_state_; }
    
    // Set data callback for received SSH data
    void set_data_callback(DataCallback callback) { data_callback_ = callback; }

    // Set resize callback for SSH ready events
    void set_resize_callback(std::function<void(int, int)> callback) { resize_callback_ = callback; }

    // Set status callback for connection status messages
    void set_status_callback(DataCallback callback) { status_callback_ = callback; }

    // Initialize log file
    static void init_log_file();
    
    // Resize SSH channel terminal size
    bool resize_terminal(int rows, int cols);
    
    // Cleanup SSH connection
    void cleanup();

private:
    // SSH event handling
    void handle_ssh_events(int status, int events);
    void process_ssh_data();
    void continue_ssh_connection();
    void perform_authentication();
    void open_ssh_channel();
    void request_pty();
    void request_locale();
    void request_shell();
    void start_polling();
    void stop_polling();
    static void on_poll_event(uv_poll_t* handle, int status, int events);
    
    
private:
    // libuv components
    uv_loop_t* loop_;
    uv_tcp_t tcp_handle_;
    uv_connect_t connect_req_;
    uv_poll_t* poll_handle_;
    bool poll_active_;
    
    // libssh2 components
    LIBSSH2_SESSION* ssh_session_;
    LIBSSH2_CHANNEL* ssh_channel_;
    
    // Connection state
    SSHState ssh_state_;
    
    // Connection parameters
    std::string host_;
    int port_;
    std::string username_;
    std::string password_;
    
    // Data callback
    DataCallback data_callback_;

    // Resize callback for SSH ready events
    std::function<void(int, int)> resize_callback_;

    // Status callback for connection status messages
    DataCallback status_callback_;
};

#endif // SSHMANAGER_H
