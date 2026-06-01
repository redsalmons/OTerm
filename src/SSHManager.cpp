#include "SSHManager.h"
#include "GlobalConfig.h"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <filesystem>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#endif
#include <cstring>

// External variables for logging
std::ofstream ssh_log_file;
bool ssh_log_initialized = false;

static const char* state_name(SSHManager::SSHState s) {
    switch (s) {
        case SSHManager::SSH_DISCONNECTED:    return "DISCONNECTED";
        case SSHManager::SSH_CONNECTING:      return "CONNECTING";
        case SSHManager::SSH_HANDSHAKING:     return "HANDSHAKING";
        case SSHManager::SSH_AUTHENTICATING:  return "AUTHENTICATING";
        case SSHManager::SSH_CHANNEL_OPENING: return "CHANNEL_OPENING";
        case SSHManager::SSH_PTY_REQUESTING:  return "PTY_REQUESTING";
        case SSHManager::SSH_SHELL_REQUESTING:return "SHELL_REQUESTING";
        case SSHManager::SSH_READY:           return "READY";
        default: return "UNKNOWN";
    }
}

void SSHManager::init_log_file() {
    if (ssh_log_initialized) {
        return;
    }

    try {
        std::filesystem::path log_dir = std::filesystem::path(GlobalConfig::GetLogPath());
        std::filesystem::create_directories(log_dir);

        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        std::tm tm;
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        std::ostringstream filename;
        filename << "ssh_" << std::put_time(&tm, "%Y%m%d_%H%M%S") << ".log";

        std::filesystem::path log_path = log_dir / filename.str();
        ssh_log_file.open(log_path, std::ios::out | std::ios::app);

        if (ssh_log_file.is_open()) {
            ssh_log_initialized = true;
            ssh_log_file << "=== SSH Log Session Started ===" << std::endl;
            ssh_log_file.flush();
        }
    } catch (const std::exception& e) {
        // If log initialization fails, silently continue without logging
    }
}

SSHManager::SSHManager()
    : loop_(nullptr), ssh_session_(nullptr), ssh_channel_(nullptr),
      ssh_state_(SSH_DISCONNECTED), poll_handle_(nullptr), poll_active_(false) {
    init_log_file();
    SSH_LOG("SSHManager created");
}

SSHManager::~SSHManager() {
    SSH_LOG("SSHManager destroyed");
    cleanup();

    if (ssh_log_file.is_open()) {
        ssh_log_file << "=== SSH Log Session Ended ===" << std::endl;
        ssh_log_file.flush();
        ssh_log_file.close();
    }
}

bool SSHManager::initialize(uv_loop_t* loop) {
    SSH_LOG("Initializing SSHManager...");
    if (!loop) {
        SSH_ERR("Invalid libuv loop");
        return false;
    }
    
    loop_ = loop;
    
    if (libssh2_init(0) != 0) {
        SSH_ERR("Failed to initialize libssh2");
        return false;
    }
    
    SSH_LOG("libssh2 initialized successfully");
    return true;
}

bool SSHManager::connect(const std::string& host, int port,
                         const std::string& username, const std::string& password,
                         const std::string& auth_method) {
    SSH_LOG("=== Starting SSH connection ===");
    SSH_LOG("  Target: " << host << ":" << port);
    SSH_LOG("  Username: " << username);
    SSH_LOG("  Auth method: " << auth_method);

    host_ = host;
    port_ = port;
    username_ = username;
    password_ = password;
    auth_method_ = auth_method;

    if (uv_tcp_init(loop_, &tcp_handle_) != 0) {
        SSH_ERR("Failed to initialize TCP handle");
        return false;
    }
    
    tcp_handle_.data = this;
    uv_tcp_nodelay(&tcp_handle_, 1);
    
    struct sockaddr_in addr;
    uv_ip4_addr(host.c_str(), port, &addr);
    SSH_LOG("Resolved address: " << host << ":" << port);
    
    connect_req_.data = this;
    if (uv_tcp_connect(&connect_req_, &tcp_handle_, (struct sockaddr*)&addr, 
                       [](uv_connect_t* req, int status) {
        SSHManager* mgr = static_cast<SSHManager*>(req->data);
        if (status < 0) {
            SSH_ERR("TCP connect failed: " << uv_strerror(status));
            mgr->ssh_state_ = SSH_DISCONNECTED;
        } else {
            SSH_LOG("TCP connection established -> " << state_name(SSH_CONNECTING));
            mgr->ssh_state_ = SSH_CONNECTING;
            mgr->continue_ssh_connection();
        }
    }) != 0) {
        SSH_ERR("Failed to initiate TCP connect");
        return false;
    }
    
    SSH_LOG("TCP connect initiated, waiting for completion...");
    return true;
}

void SSHManager::continue_ssh_connection() {
    SSH_LOG("Starting SSH session handshake...");
    
    uv_os_fd_t sockfd;
    if (uv_fileno((uv_handle_t*)&tcp_handle_, &sockfd) != 0) {
        SSH_ERR("Failed to get socket fd");
        ssh_state_ = SSH_DISCONNECTED;
        return;
    }
    
    ssh_session_ = libssh2_session_init();
    if (!ssh_session_) {
        SSH_ERR("Failed to create SSH session");
        ssh_state_ = SSH_DISCONNECTED;
        return;
    }
    
    libssh2_session_set_blocking(ssh_session_, 0);
    SSH_LOG("SSH session created, non-blocking mode set");
    
    int rc = libssh2_session_handshake(ssh_session_, (int)sockfd);
    if (rc == LIBSSH2_ERROR_EAGAIN) {
        SSH_LOG("Handshake pending (EAGAIN), waiting for socket...");
        ssh_state_ = SSH_HANDSHAKING;
        start_polling();
        return;
    } else if (rc != 0) {
        SSH_ERR("Handshake failed: " << rc);
        ssh_state_ = SSH_DISCONNECTED;
        return;
    }
    
    SSH_LOG("Handshake completed immediately -> " << state_name(SSH_AUTHENTICATING));
    ssh_state_ = SSH_AUTHENTICATING;
    perform_authentication();
}

bool SSHManager::send_data(const char* data, int length) {
    if (!is_ready()) {
        // Silently fail during handshake - don't log errors
        return false;
    }
    
    int rc = libssh2_channel_write(ssh_channel_, data, length);
    if (rc < 0) {
        SSH_ERR("Channel write failed: " << rc);
    }
    return rc >= 0;
}

bool SSHManager::resize_terminal(int rows, int cols) {
    if (!is_ready()) {
        SSH_ERR("Cannot resize: SSH not ready");
        return false;
    }
    
    SSH_LOG("Resizing terminal to " << rows << "x" << cols);
    int rc = libssh2_channel_request_pty_size_ex(ssh_channel_, cols, rows, 0, 0);
    if (rc == 0) {
        SSH_LOG("Terminal resized successfully: " << rows << "x" << cols);
        return true;
    } else {
        SSH_ERR("Terminal resize failed: " << rc);
        return false;
    }
}

void SSHManager::cleanup() {
    SSH_LOG("Cleaning up SSH resources...");

    stop_polling();

    if (ssh_channel_) {
        SSH_LOG("Freeing SSH channel");
        libssh2_channel_free(ssh_channel_);
        ssh_channel_ = nullptr;
    }

    if (ssh_session_) {
        SSH_LOG("Freeing SSH session");
        libssh2_session_free(ssh_session_);
        ssh_session_ = nullptr;
    }

    if (!uv_is_closing((uv_handle_t*)&tcp_handle_)) {
        SSH_LOG("Closing TCP handle");
        uv_close((uv_handle_t*)&tcp_handle_, nullptr);
    }

    ssh_state_ = SSH_DISCONNECTED;
    SSH_LOG("Cleanup complete -> " << state_name(SSH_DISCONNECTED));
}

void SSHManager::handle_ssh_events(int status, int events) {
    if (status < 0) {
        SSH_ERR("Event error: " << status);
        return;
    }

    // SSH_LOG("handle_ssh_events: state=" << state_name(ssh_state_) << ", events=" << events);

    uv_os_fd_t sockfd;
    if (uv_fileno((uv_handle_t*)&tcp_handle_, &sockfd) != 0) {
        SSH_ERR("Failed to get socket fd in event handler");
        return;
    }

    switch (ssh_state_) {
        case SSH_HANDSHAKING: {
            int rc = libssh2_session_handshake(ssh_session_, (int)sockfd);
            if (rc == 0) {
                SSH_LOG("Handshake completed -> " << state_name(SSH_AUTHENTICATING));
                ssh_state_ = SSH_AUTHENTICATING;
                perform_authentication();
            } else if (rc == LIBSSH2_ERROR_EAGAIN) {
                return;
            } else {
                SSH_ERR("Handshake failed: " << rc);
                ssh_state_ = SSH_DISCONNECTED;
                stop_polling();
            }
            break;
        }

        case SSH_AUTHENTICATING: {
            perform_authentication();
            break;
        }

        case SSH_CHANNEL_OPENING: {
            open_ssh_channel();
            break;
        }

        case SSH_PTY_REQUESTING: {
            request_pty();
            break;
        }

        case SSH_SHELL_REQUESTING: {
            request_shell();
            break;
        }

        case SSH_READY: {
            // Process SSH data when socket is readable
            if (events & UV_READABLE) {
                process_ssh_data();
            }
            break;
        }

        default:
            break;
    }
}

void SSHManager::perform_authentication() {
    // SSH_LOG("Authenticating as '" << username_ << "'...");
    int rc = 0;

    if (auth_method_ == "key") {
        // Key authentication using memory
        SSH_LOG("Using key authentication");
        SSH_LOG("  Private key length: " << password_.length());

        rc = libssh2_userauth_publickey_frommemory(
            ssh_session_,
            username_.c_str(), username_.length(),
            nullptr, 0,  // No public key data (will be derived from private key)
            password_.c_str(), password_.length(),  // Private key data
            nullptr  // No passphrase
        );

        if (rc != 0) {
            SSH_ERR("libssh2_userauth_publickey_frommemory failed with error: " << rc);
            SSH_ERR("  This may indicate:");
            SSH_ERR("  1. Invalid key format");
            SSH_ERR("  2. Key requires a passphrase");
            SSH_ERR("  3. Public key needs to be provided separately");
        }
    } else {
        // Password authentication
        SSH_LOG("Using password authentication");
        rc = libssh2_userauth_password(ssh_session_, username_.c_str(), password_.c_str());
    }

    if (rc == 0) {
        SSH_LOG("Authentication successful -> " << state_name(SSH_CHANNEL_OPENING));
        ssh_state_ = SSH_CHANNEL_OPENING;
        open_ssh_channel();
    } else if (rc == LIBSSH2_ERROR_EAGAIN) {
        // SSH_LOG("Authentication pending (EAGAIN), retrying...");
        start_polling();
        return;
    } else {
        SSH_ERR("Authentication failed: " << rc);
        ssh_state_ = SSH_DISCONNECTED;
        stop_polling();
    }
}

void SSHManager::open_ssh_channel() {
    // SSH_LOG("Opening SSH channel...");
    ssh_channel_ = libssh2_channel_open_session(ssh_session_);
    if (ssh_channel_) {
        SSH_LOG("Channel opened -> " << state_name(SSH_PTY_REQUESTING));
        ssh_state_ = SSH_PTY_REQUESTING;
        request_pty();
    } else if (libssh2_session_last_errno(ssh_session_) == LIBSSH2_ERROR_EAGAIN) {
        // SSH_LOG("Channel open pending (EAGAIN), retrying...");
        start_polling();
        return;
    } else {
        SSH_ERR("Failed to open channel");
        ssh_state_ = SSH_DISCONNECTED;
        stop_polling();
    }
}

void SSHManager::request_pty() {
    // SSH_LOG("Requesting PTY (xterm-256color)...");
    int pty_result = libssh2_channel_request_pty(ssh_channel_, "xterm-256color");
    if (pty_result == 0) {
        SSH_LOG("PTY granted -> " << state_name(SSH_SHELL_REQUESTING));
        ssh_state_ = SSH_SHELL_REQUESTING;
        request_locale();
    } else if (pty_result == LIBSSH2_ERROR_EAGAIN) {
        // SSH_LOG("PTY request pending (EAGAIN), retrying...");
        start_polling();
        return;
    } else {
        SSH_ERR("PTY request failed: " << pty_result);
        ssh_state_ = SSH_DISCONNECTED;
        stop_polling();
    }
}

void SSHManager::request_locale() {
    // Try to set UTF-8 locale environment variables
    // Note: Many SSH servers reject setenv, so we proceed regardless of result
    int lang_result = libssh2_channel_setenv(ssh_channel_, "LANG", "zh_CN.UTF-8");
    int lc_all_result = libssh2_channel_setenv(ssh_channel_, "LC_ALL", "zh_CN.UTF-8");
    
    if (lang_result == 0 || lc_all_result == 0) {
        SSH_LOG("Locale environment variables set successfully");
    } else if (lang_result == LIBSSH2_ERROR_EAGAIN || lc_all_result == LIBSSH2_ERROR_EAGAIN) {
        // Locale setting pending, but proceed anyway
        SSH_LOG("Locale setting pending, proceeding to shell");
    } else {
        // Most SSH servers reject setenv - this is expected
        SSH_LOG("Locale setenv rejected by server (expected), proceeding to shell");
    }
    
    // Proceed to shell request regardless of setenv result
    request_shell();
}

void SSHManager::request_shell() {
    // SSH_LOG("Requesting shell...");
    int shell_result = libssh2_channel_shell(ssh_channel_);
    if (shell_result == 0) {
        SSH_LOG("=== SSH CONNECTION READY ===");
        SSH_LOG("Shell started, SSH is now READY");
        ssh_state_ = SSH_READY;
        // Don't stop polling - keep it active for data reading in READY state

        // Trigger resize callback to send initial terminal size to SSH
        // This ensures vi and other applications receive the correct terminal size
        if (resize_callback_) {
            SSH_LOG("SSH connection ready, triggering resize callback to send terminal size");
            resize_callback_(-1, -1); // Special values to indicate initial resize
        }

        SSH_LOG("SSH connection ready, polling continues for data reading");
    } else if (shell_result == LIBSSH2_ERROR_EAGAIN) {
        // SSH_LOG("Shell request pending (EAGAIN), retrying...");
        start_polling();
        return;
    } else {
        SSH_ERR("Shell request failed: " << shell_result);
        ssh_state_ = SSH_DISCONNECTED;
        stop_polling();
    }
}

void SSHManager::process_ssh_data() {
    if (!ssh_channel_) return;

    char buffer[8192];
    int rc = libssh2_channel_read(ssh_channel_, buffer, sizeof(buffer));

    if (rc > 0) {
        if (data_callback_) {
            data_callback_(buffer, rc);
        }
        SSH_LOG("Read " << rc << " bytes from SSH channel");
    } else if (rc == LIBSSH2_ERROR_EAGAIN) {
        // No data available, will be called again on next poll event
    } else if (rc < 0) {
        SSH_ERR("Channel read error: " << rc);
    } else {
        SSH_LOG("Channel closed (rc=0)");
    }
}

void SSHManager::start_polling() {
    if (poll_active_) {
        // SSH_LOG("Polling already active, skipping");
        return;
    }

    // SSH_LOG("start_polling called");

    uv_os_fd_t sockfd;
    if (uv_fileno((uv_handle_t*)&tcp_handle_, &sockfd) != 0) {
        SSH_ERR("Failed to get socket fd for polling");
        return;
    }

    poll_handle_ = new uv_poll_t();
    poll_handle_->data = this;

    if (uv_poll_init_socket(loop_, poll_handle_, (uv_os_sock_t)sockfd) != 0) {
        SSH_ERR("Failed to initialize poll handle");
        delete poll_handle_;
        poll_handle_ = nullptr;
        return;
    }

    if (uv_poll_start(poll_handle_, UV_READABLE | UV_WRITABLE, on_poll_event) != 0) {
        SSH_ERR("Failed to start polling");
        uv_close((uv_handle_t*)poll_handle_, [](uv_handle_t* handle) {
            delete reinterpret_cast<uv_poll_t*>(handle);
        });
        poll_handle_ = nullptr;
        return;
    }

    poll_active_ = true;
    SSH_LOG("Started polling socket for SSH events");
}

void SSHManager::stop_polling() {
    if (!poll_active_ || !poll_handle_) {
        return;
    }

    uv_poll_stop(poll_handle_);
    if (!uv_is_closing((uv_handle_t*)poll_handle_)) {
        uv_close((uv_handle_t*)poll_handle_, [](uv_handle_t* handle) {
            delete reinterpret_cast<uv_poll_t*>(handle);
        });
    }
    poll_handle_ = nullptr;
    poll_active_ = false;
    SSH_LOG("Stopped polling socket");
}

void SSHManager::on_poll_event(uv_poll_t* handle, int status, int events) {
    // SSH_LOG("Poll event triggered: status=" << status << ", events=" << events);
    SSHManager* mgr = static_cast<SSHManager*>(handle->data);
    if (mgr) {
        mgr->handle_ssh_events(status, events);
    }
}

