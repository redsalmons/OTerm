#include "SSHTerminalContainer.h"
#include "SSHManager.h"
#include "TerminalPanel.h"
#include <iostream>
#include <thread>

wxDEFINE_EVENT(wxEVT_SSH_DNS_RESOLVED, wxCommandEvent);

SSHTerminalContainer::SSHTerminalContainer(int rows, int cols, const DeviceConfig& config)
    : m_deviceConfig(config),
      m_rows(rows),
      m_cols(cols),
      m_uiHandler(nullptr),
      m_socket(nullptr),
      m_sshSession(nullptr),
      m_sshChannel(nullptr),
      m_sshState(SSH_DISCONNECTED),
      m_vtermManager(),
      m_frontBuffer(),
      m_backBuffer(),
      m_authRetryCount(0),
      m_pendingInput(""),
      m_hasDamage(false) {
    
    m_frontBuffer.resize(rows, cols);
    m_backBuffer.resize(rows, cols);

    // Initialize VTerm Manager
    if (!m_vtermManager.initialize(rows, cols)) {
        std::cerr << "SSHTerminalContainer: Failed to initialize VTermManager" << std::endl;
    }

    // Set damage callback for VTerm updates
    m_vtermManager.set_damage_callback([this](VTermRect rect, const std::vector<std::vector<VTermManager::TerminalCell>>& cells) {
        m_hasDamage = true;
    });

    // Set up timer event bindings
    m_keepAliveTimer.SetOwner(this, KEEPALIVE_TIMER_ID);
    Bind(wxEVT_TIMER, &SSHTerminalContainer::OnKeepAliveTimer, this, KEEPALIVE_TIMER_ID);
    m_connectTimer.SetOwner(this, CONNECT_TIMER_ID);
    Bind(wxEVT_TIMER, &SSHTerminalContainer::OnConnectTimeout, this, CONNECT_TIMER_ID);
    Bind(wxEVT_SSH_DNS_RESOLVED, &SSHTerminalContainer::OnDnsResolved, this);
    
    SSH_LOG("SSHTerminalContainer created for " << config.address);
}

SSHTerminalContainer::~SSHTerminalContainer() {
    SSH_LOG("SSHTerminalContainer destructor called");
    m_alive->store(false);
    Cleanup();
}

void SSHTerminalContainer::SetUIHandler(wxWindow* ui_handler) {
    m_uiHandler = ui_handler;
    SSH_LOG("SSHTerminalContainer: UI handler set to " << ui_handler);
}

void SSHTerminalContainer::ClearUIHandler() {
    m_uiHandler = nullptr;
    SSH_LOG("SSHTerminalContainer: UI handler cleared");
}

void SSHTerminalContainer::StopTerminal() {
    SSH_LOG("SSHTerminalContainer::StopTerminal called");
    Cleanup();
}

bool SSHTerminalContainer::IsSessionAlive() const {
    return m_sshState != SSH_DISCONNECTED;
}

void SSHTerminalContainer::QueueInput(const std::string& input) {
    if (m_sshState == SSH_PROMPTING_USERNAME || m_sshState == SSH_PROMPTING_PASSWORD) {
        ArmConnectTimeout(); // user typing counts as progress
    }
    if (m_sshState == SSH_PROMPTING_USERNAME) {
        for (char c : input) {
            if (c == '\r' || c == '\n') {
                m_vtermManager.write_input("\r\n", 2);
                m_deviceConfig.username = m_pendingInput;
                m_pendingInput.clear();
                StartPasswordPrompt();
            } else if (c == '\x7f' || c == '\x08') {
                if (!m_pendingInput.empty()) {
                    m_pendingInput.pop_back();
                    const char* bs = "\x08 \x08";
                    m_vtermManager.write_input(bs, 3);
                }
            } else {
                m_pendingInput += c;
                m_vtermManager.write_input(&c, 1);
            }
        }
        UpdateBackBuffer();
        TriggerDamage();
    } else if (m_sshState == SSH_PROMPTING_PASSWORD) {
        for (char c : input) {
            if (c == '\r' || c == '\n') {
                m_vtermManager.write_input("\r\n", 2);
                m_deviceConfig.password = m_pendingInput;
                m_pendingInput.clear();
                
                m_sshState = SSH_AUTHENTICATING;
                PerformAuthentication();
            } else if (c == '\x7f' || c == '\x08') {
                if (!m_pendingInput.empty()) {
                    m_pendingInput.pop_back();
                }
            } else {
                m_pendingInput += c;
            }
        }
        UpdateBackBuffer();
        TriggerDamage();
    } else if (m_sshState == SSH_READY && m_sshChannel) {
        m_outQueue.append(input);
        FlushOutbound();
    }
}

void SSHTerminalContainer::Resize(int rows, int cols) {
    if (rows <= 0 || cols <= 0) return;
    
    SSH_LOG("SSHTerminalContainer Resize to " << rows << "x" << cols);
    m_rows = rows;
    m_cols = cols;

    m_vtermManager.resize(rows, cols);
    
    m_frontBuffer.resize(rows, cols);
    m_frontBuffer.clear();
    m_backBuffer.resize(rows, cols);
    m_backBuffer.clear();

    if (m_sshState == SSH_READY && m_sshChannel) {
        libssh2_channel_request_pty_size_ex(m_sshChannel, cols, rows, 0, 0);
    }

    UpdateBackBuffer();
    TriggerDamage();
}

void SSHTerminalContainer::Scroll(int lines) {
    if (lines > 0) {
        m_vtermManager.scroll_up(lines);
    } else {
        m_vtermManager.scroll_down(-lines);
    }
    UpdateBackBuffer();
    TriggerDamage();
}

void SSHTerminalContainer::ResetScrollToBottom() {
    m_vtermManager.set_scroll_offset(0);
    UpdateBackBuffer();
    TriggerDamage();
}

const ScreenBuffer* SSHTerminalContainer::GetFrontBuffer() const {
    return &m_frontBuffer;
}

void SSHTerminalContainer::CopyFrontBuffer(ScreenBuffer& dest) const {
    dest = m_frontBuffer;
}

bool SSHTerminalContainer::IsInAlternateScreen() const {
    return m_vtermManager.is_in_alternate_screen();
}

int SSHTerminalContainer::GetScrollOffset() const {
    return m_vtermManager.get_scroll_offset();
}

void SSHTerminalContainer::Connect() {
    SSH_LOG("SSHTerminalContainer::Connect initiated");
    Cleanup();

    if (!EnsureLibssh2Init()) {
        SSH_ERR("Failed to initialize libssh2");
        SendStatusMessage("\r\nFailed to initialize SSH library.\r\n");
        return;
    }

    m_sshState = SSH_CONNECTING;
    m_authRetryCount = 0;

    SendStatusMessage("Connecting to " + m_deviceConfig.address + ":" + m_deviceConfig.port + "...\r\n");

    m_socket = new wxSocketClient(wxSOCKET_NOWAIT);
    m_socket->SetEventHandler(*this, SOCKET_ID);
    m_socket->SetNotify(wxSOCKET_CONNECTION_FLAG | wxSOCKET_LOST_FLAG);
    m_socket->Notify(true);

    Bind(wxEVT_SOCKET, &SSHTerminalContainer::OnSocketEvent, this, SOCKET_ID);

    ArmConnectTimeout();

    if (IsNumericIPv4(m_deviceConfig.address)) {
        ConnectSocket(m_deviceConfig.address);
    } else {
        // Resolve on a worker thread so DNS cannot stall the UI
        SendStatusMessage("Resolving " + m_deviceConfig.address + "...\r\n");
        std::string host = m_deviceConfig.address;
        auto alive = m_alive;
        std::thread([this, host, alive]() {
            std::string ip = ResolveIPv4(host);
            if (!alive->load()) return;
            wxCommandEvent evt(wxEVT_SSH_DNS_RESOLVED);
            evt.SetString(wxString::FromUTF8(ip));
            wxQueueEvent(this, evt.Clone());
        }).detach();
    }
}

void SSHTerminalContainer::ConnectSocket(const std::string& ip) {
    if (!m_socket) return;

    wxIPV4address addr;
    addr.Hostname(ip); // numeric IP — does not block
    addr.Service(m_deviceConfig.port);

    m_socket->Connect(addr, false); // Asynchronous connect
}

void SSHTerminalContainer::OnDnsResolved(wxCommandEvent& event) {
    if (!m_socket || m_sshState != SSH_CONNECTING) return;

    std::string ip = event.GetString().ToStdString();
    if (ip.empty()) {
        SSH_ERR("DNS resolution failed for " << m_deviceConfig.address);
        SendStatusMessage("\r\nCould not resolve host: " + m_deviceConfig.address + "\r\n");
        Cleanup();
        return;
    }

    SSH_LOG("Resolved " << m_deviceConfig.address << " -> " << ip);
    ArmConnectTimeout();
    ConnectSocket(ip);
}

void SSHTerminalContainer::ArmConnectTimeout() {
    if (m_sshState != SSH_READY && m_sshState != SSH_DISCONNECTED) {
        m_connectTimer.Start(CONNECT_TIMEOUT_MS, wxTIMER_ONE_SHOT);
    }
}

void SSHTerminalContainer::OnConnectTimeout(wxTimerEvent& event) {
    if (m_sshState == SSH_DISCONNECTED || m_sshState == SSH_READY) {
        return;
    }
    SSH_ERR("Connection timed out in state " << GetStateName(m_sshState));
    SendStatusMessage("\r\nConnection timed out.\r\n");
    Cleanup();
}

void SSHTerminalContainer::OnSocketEvent(wxSocketEvent& event) {
    if (!m_socket) return;

    ArmConnectTimeout(); // any socket activity counts as progress

    switch (event.GetSocketEvent()) {
        case wxSOCKET_CONNECTION: {
            SSH_LOG("TCP Socket connection established. Proceeding to SSH handshake.");
            SendStatusMessage("TCP connection established. Starting SSH handshake...\r\n");
            
            m_sshState = SSH_HANDSHAKING;
            
            // Initialize libssh2 session
            m_sshSession = libssh2_session_init();
            if (!m_sshSession) {
                SSH_ERR("Failed to initialize libssh2 session");
                SendStatusMessage("\r\nFailed to initialize SSH session.\r\n");
                Cleanup();
                return;
            }

            libssh2_session_set_blocking(m_sshSession, 0); // Non-blocking
            
#ifndef _WIN32
            // Turn off wxSocket notifications on Unix; we will use the raw FD source.
            // If the FD source cannot be registered, keep socket notifications as fallback.
            wxEventLoopBase* loop = wxEventLoop::GetActive();
            if (loop && m_socket->GetSocket() >= 0) {
                m_fdSource = loop->AddSourceForFD(m_socket->GetSocket(), this, wxEVENT_SOURCE_INPUT | wxEVENT_SOURCE_OUTPUT);
            }
            if (m_fdSource) {
                m_socket->Notify(false);
            } else {
                SSH_ERR("Failed to register FD source; falling back to wxSocket notifications");
                m_socket->SetNotify(wxSOCKET_INPUT_FLAG | wxSOCKET_OUTPUT_FLAG | wxSOCKET_LOST_FLAG);
            }
#endif

            ContinueHandshake();
            break;
        }
        case wxSOCKET_INPUT: {
            switch (m_sshState) {
                case SSH_HANDSHAKING:
                    ContinueHandshake();
                    break;
                case SSH_AUTHENTICATING:
                    PerformAuthentication();
                    break;
                case SSH_CHANNEL_OPENING:
                    OpenSSHChannel();
                    break;
                case SSH_PTY_REQUESTING:
                    RequestPTY();
                    break;
                case SSH_SHELL_REQUESTING:
                    RequestShell();
                    break;
                case SSH_READY:
                    FlushOutbound();
                    ProcessSSHData();
                    break;
                default:
                    break;
            }
            break;
        }
        case wxSOCKET_OUTPUT: {
            // Write events can drive handshakes/auth too if libssh2 was waiting for a write direction
            switch (m_sshState) {
                case SSH_HANDSHAKING:
                    ContinueHandshake();
                    break;
                case SSH_AUTHENTICATING:
                    PerformAuthentication();
                    break;
                case SSH_CHANNEL_OPENING:
                    OpenSSHChannel();
                    break;
                case SSH_PTY_REQUESTING:
                    RequestPTY();
                    break;
                case SSH_SHELL_REQUESTING:
                    RequestShell();
                    break;
                case SSH_READY:
                    FlushOutbound();
                    break;
                default:
                    break;
            }
            break;
        }
        case wxSOCKET_LOST: {
            SSH_ERR("Socket connection lost unexpectedly.");
            SendStatusMessage("\r\nConnection closed by remote host.\r\n");
            Cleanup();
            break;
        }
        default:
            break;
    }
}

#ifndef _WIN32
void SSHTerminalContainer::OnReadWaiting() {
    DriveStateMachine();
}

void SSHTerminalContainer::OnWriteWaiting() {
    DriveStateMachine();
}

void SSHTerminalContainer::OnExceptionWaiting() {
    SSH_ERR("Socket error or connection lost (exception event)");
    SendStatusMessage("\r\nConnection lost.\r\n");
    Cleanup();
}
#endif

void SSHTerminalContainer::DriveStateMachine() {
    ArmConnectTimeout(); // any fd activity counts as progress
    switch (m_sshState) {
        case SSH_HANDSHAKING:
            ContinueHandshake();
            break;
        case SSH_AUTHENTICATING:
            PerformAuthentication();
            break;
        case SSH_CHANNEL_OPENING:
            OpenSSHChannel();
            break;
        case SSH_PTY_REQUESTING:
            RequestPTY();
            break;
        case SSH_SHELL_REQUESTING:
            RequestShell();
            break;
        case SSH_READY:
            FlushOutbound();
            ProcessSSHData();
            break;
        default:
            break;
    }
}

void SSHTerminalContainer::OnKeepAliveTimer(wxTimerEvent& event) {
    if (m_sshState == SSH_READY && m_sshSession && m_sshChannel) {
        int seconds_to_next = 0;
        libssh2_keepalive_send(m_sshSession, &seconds_to_next);
    }
}

void SSHTerminalContainer::ContinueHandshake() {
    if (!m_socket || !m_sshSession) return;

    int sockfd = m_socket->GetSocket();
    int rc = libssh2_session_handshake(m_sshSession, sockfd);
    
    if (rc == 0) {
        SSH_LOG("SSH Handshake completed.");
        StartLoginPrompt();
    } else if (rc == LIBSSH2_ERROR_EAGAIN) {
        HandleDirections();
    } else {
        SSH_ERR("SSH Handshake failed: " << rc);
        SendStatusMessage("\r\nSSH handshake failed with error: " + std::to_string(rc) + "\r\n");
        Cleanup();
    }
}

void SSHTerminalContainer::StartLoginPrompt() {
    if (m_deviceConfig.username.empty()) {
        m_sshState = SSH_PROMPTING_USERNAME;
        SendStatusMessage("login: ");
    } else {
        SendStatusMessage("login: " + m_deviceConfig.username + "\r\n");
        StartPasswordPrompt();
    }
}

void SSHTerminalContainer::StartPasswordPrompt() {
    if (m_deviceConfig.password.empty()) {
        m_sshState = SSH_PROMPTING_PASSWORD;
        SendStatusMessage("password: ");
    } else {
        m_sshState = SSH_AUTHENTICATING;
        PerformAuthentication();
    }
}

void SSHTerminalContainer::PerformAuthentication() {
    if (!m_sshSession) return;

    std::string username = m_deviceConfig.username;
    std::string password = m_deviceConfig.password;
    int rc = 0;

    if (m_deviceConfig.auth_method == "key") {
        rc = libssh2_userauth_publickey_frommemory(
            m_sshSession,
            username.c_str(), username.length(),
            nullptr, 0,
            password.c_str(), password.length(),
            nullptr
        );
    } else {
        rc = libssh2_userauth_password(m_sshSession, username.c_str(), password.c_str());
    }

    if (rc == 0) {
        SSH_LOG("SSH Authentication successful.");
        m_authRetryCount = 0;
        // Persist the working password to the vault so file transfer and
        // future sessions can authenticate without re-prompting
        if (!password.empty() && !m_deviceConfig.id.empty()) {
            DeviceConfig::StoreCredential(m_deviceConfig.id, password);
        }
        m_sshState = SSH_CHANNEL_OPENING;
        OpenSSHChannel();
    } else if (rc == LIBSSH2_ERROR_EAGAIN) {
        HandleDirections();
    } else {
        m_authRetryCount++;
        SSH_ERR("Authentication failed (" << m_authRetryCount << "/3)");

        if (m_authRetryCount >= 3) {
            SendStatusMessage("\r\nAuthentication failed (3/3). Connection closed.\r\n");
            Cleanup();
        } else {
            m_deviceConfig.password.clear();
            SendStatusMessage("\r\nAuthentication failed (" + std::to_string(m_authRetryCount) + "/3). Please try again.\r\n");
            StartPasswordPrompt();
        }
    }
}

void SSHTerminalContainer::OpenSSHChannel() {
    if (!m_sshSession) return;

    m_sshChannel = libssh2_channel_open_session(m_sshSession);
    if (m_sshChannel) {
        SSH_LOG("SSH Channel opened.");
        m_sshState = SSH_PTY_REQUESTING;
        RequestPTY();
    } else if (libssh2_session_last_errno(m_sshSession) == LIBSSH2_ERROR_EAGAIN) {
        HandleDirections();
    } else {
        SSH_ERR("Failed to open SSH session channel");
        SendStatusMessage("\r\nFailed to open session channel.\r\n");
        Cleanup();
    }
}

void SSHTerminalContainer::RequestPTY() {
    if (!m_sshChannel) return;

    int rc = libssh2_channel_request_pty(m_sshChannel, "xterm-256color");
    if (rc == 0) {
        SSH_LOG("PTY granted.");
        m_sshState = SSH_SHELL_REQUESTING;
        RequestShell();
    } else if (rc == LIBSSH2_ERROR_EAGAIN) {
        HandleDirections();
    } else {
        SSH_ERR("PTY request failed: " << rc);
        SendStatusMessage("\r\nPTY request failed.\r\n");
        Cleanup();
    }
}

void SSHTerminalContainer::RequestShell() {
    if (!m_sshChannel) return;

    int rc = libssh2_channel_shell(m_sshChannel);
    if (rc == 0) {
        SSH_LOG("SSH Shell successfully initiated. Connection fully ready.");
        m_sshState = SSH_READY;
        m_connectTimer.Stop();

        // Send initial resize
        libssh2_channel_request_pty_size_ex(m_sshChannel, m_cols, m_rows, 0, 0);

        // Set up Keep-Alive heartbeats (every 10 seconds)
        libssh2_keepalive_config(m_sshSession, 1, 10);
        m_keepAliveTimer.Start(10000);

        // Steady state: monitor inbound data only. Outbound interest is
        // re-registered on demand by HandleDirections() when writes block.
        WatchInboundOnly();
        // Process any data that might already be waiting
        ProcessSSHData();
    } else if (rc == LIBSSH2_ERROR_EAGAIN) {
        HandleDirections();
    } else {
        SSH_ERR("Shell request failed: " << rc);
        SendStatusMessage("\r\nFailed to request shell.\r\n");
        Cleanup();
    }
}

void SSHTerminalContainer::ProcessSSHData() {
    if (!m_sshChannel) return;

    char buffer[65536];
    bool has_data = false;

    while (true) {
        int rc = libssh2_channel_read(m_sshChannel, buffer, sizeof(buffer));
        if (rc > 0) {
            m_vtermManager.write_input_no_flush(buffer, rc);
            has_data = true;
        } else if (rc == LIBSSH2_ERROR_EAGAIN) {
            break;
        } else if (rc == 0) {
            SSH_LOG("SSH Channel EOF received");
            SendStatusMessage("\r\nConnection closed by remote host.\r\n");
            Cleanup();
            return;
        } else {
            SSH_ERR("Channel read error: " << rc);
            Cleanup();
            return;
        }
    }

    // Drain stderr/extended data so it cannot starve the channel window.
    // On a PTY shell this is normally empty (stderr is merged into stdout).
    while (libssh2_channel_read_stderr(m_sshChannel, buffer, sizeof(buffer)) > 0) {
    }

    if (has_data) {
        m_vtermManager.flush_damage();
        UpdateBackBuffer();
        TriggerDamage();
    }

    if (m_sshChannel && libssh2_channel_eof(m_sshChannel)) {
        SSH_LOG("SSH channel EOF detected.");
        SendStatusMessage("\r\nConnection closed by remote host.\r\n");
        Cleanup();
    }
}

void SSHTerminalContainer::FlushOutbound() {
    if (!m_sshChannel || m_outQueue.empty()) {
        return;
    }

    while (!m_outQueue.empty()) {
        int rc = libssh2_channel_write(m_sshChannel, m_outQueue.data(), (int)m_outQueue.size());
        if (rc > 0) {
            m_outQueue.erase(0, (size_t)rc);
        } else if (rc == LIBSSH2_ERROR_EAGAIN) {
            HandleDirections(); // registers outbound interest so we get woken up
            return;
        } else {
            SSH_ERR("SSHTerminalContainer: Channel write failed: " << rc);
            m_outQueue.clear();
            return;
        }
    }

    // Backlog fully drained: drop any stale outbound interest.
    WatchInboundOnly();
}

void SSHTerminalContainer::WatchInboundOnly() {
    if (!m_socket) return;

#ifndef _WIN32
    if (m_fdSource) {
        int sockfd = m_socket->GetSocket();
        if (sockfd >= 0) {
            delete m_fdSource;
            m_fdSource = nullptr;
            wxEventLoopBase* loop = wxEventLoop::GetActive();
            if (loop) {
                m_fdSource = loop->AddSourceForFD(sockfd, this, wxEVENT_SOURCE_INPUT);
            }
            if (m_fdSource) {
                return;
            }
            // Re-registration failed: fall through to wxSocket notifications
        }
    }
#endif

    m_socket->SetNotify(wxSOCKET_INPUT_FLAG | wxSOCKET_LOST_FLAG);
    m_socket->Notify(true);
}

void SSHTerminalContainer::SendStatusMessage(const std::string& msg) {
    m_vtermManager.write_input(msg.c_str(), msg.length());
    UpdateBackBuffer();
    TriggerDamage();
}

void SSHTerminalContainer::Cleanup() {
    m_keepAliveTimer.Stop();
    m_connectTimer.Stop();

#ifndef _WIN32
    if (m_fdSource) {
        delete m_fdSource;
        m_fdSource = nullptr;
    }
#endif

    // Close socket first to stop network I/O
    if (m_socket) {
        m_socket->Notify(false);
        m_socket->Close();
        m_socket->Destroy();
        m_socket = nullptr;
    }

    // Clean up SSH resources (socket is already closed, so these should be fast)
    if (m_sshChannel) {
        libssh2_channel_free(m_sshChannel);
        m_sshChannel = nullptr;
    }

    if (m_sshSession) {
        libssh2_session_free(m_sshSession);
        m_sshSession = nullptr;
    }

    m_sshState = SSH_DISCONNECTED;
    m_pendingInput.clear();
    m_outQueue.clear();
}

void SSHTerminalContainer::HandleDirections() {
    if (!m_socket || !m_sshSession) return;

#ifndef _WIN32
    if (m_fdSource) {
        int sockfd = m_socket->GetSocket();
        if (sockfd >= 0) {
            int directions = libssh2_session_block_directions(m_sshSession);
            int flags = 0;
            if (directions & LIBSSH2_SESSION_BLOCK_INBOUND) {
                flags |= wxEVENT_SOURCE_INPUT;
            }
            if (directions & LIBSSH2_SESSION_BLOCK_OUTBOUND) {
                flags |= wxEVENT_SOURCE_OUTPUT;
            }

            // Default to INPUT if no direction is specified
            if (flags == 0) {
                flags |= wxEVENT_SOURCE_INPUT;
            }

            if (m_fdSource) {
                delete m_fdSource;
                m_fdSource = nullptr;
            }

            wxEventLoopBase* loop = wxEventLoop::GetActive();
            if (loop) {
                m_fdSource = loop->AddSourceForFD(sockfd, this, flags);
            }
            return;
        }
    }
#endif

    int directions = libssh2_session_block_directions(m_sshSession);
    int flags = wxSOCKET_LOST_FLAG;

    if (directions & LIBSSH2_SESSION_BLOCK_INBOUND) {
        flags |= wxSOCKET_INPUT_FLAG;
    }
    if (directions & LIBSSH2_SESSION_BLOCK_OUTBOUND) {
        flags |= wxSOCKET_OUTPUT_FLAG;
    }

    // Default to INPUT if no direction is specified
    if (flags == wxSOCKET_LOST_FLAG) {
        flags |= wxSOCKET_INPUT_FLAG;
    }

    // Reached only when there is no FD source (Windows always, or as a Unix
    // fallback) — socket notifications may have been disabled, so re-enable.
    m_socket->SetNotify(flags);
    m_socket->Notify(true);
}

void SSHTerminalContainer::SwapBuffers() {
    VTermPos cursor_pos = m_vtermManager.get_cursor_pos();
    m_backBuffer.cursor_row = cursor_pos.row;
    m_backBuffer.cursor_col = cursor_pos.col;

    std::swap(m_frontBuffer, m_backBuffer);
}

void SSHTerminalContainer::UpdateBackBuffer() {
    int vterm_rows = m_vtermManager.get_rows();
    for (int row = 0; row < vterm_rows; ++row) {
        const auto& row_cells = m_vtermManager.get_screen_row(row);
        for (int col = 0; col < m_cols && col < (int)row_cells.size(); ++col) {
            const auto& cell = row_cells[col];
            CellInstance& inst = m_backBuffer.cells[row][col];
            inst.cell_x = (float)col;
            inst.cell_y = (float)row;
            inst.fg_color = cell.fg_color;
            inst.bg_color = cell.bg_color;
            inst.char_code = cell.char_code;
            inst.width = cell.width;
            inst.attrs = cell.attrs;
        }
    }

    VTermPos cursor_pos = m_vtermManager.get_cursor_pos();
    m_backBuffer.cursor_row = cursor_pos.row;
    m_backBuffer.cursor_col = cursor_pos.col;

    SwapBuffers();
}

void SSHTerminalContainer::TriggerDamage() {
    if (m_uiHandler) {
        // Try to call UpdateCanvasFromTerminal if it's a TerminalPanel
        TerminalPanel* panel = dynamic_cast<TerminalPanel*>(m_uiHandler);
        if (panel) {
            panel->UpdateCanvasFromTerminal();
        } else {
            // Fallback to Refresh
            m_uiHandler->Refresh();
        }
    }
}

const char* SSHTerminalContainer::GetStateName(SSHState state) {
    switch (state) {
        case SSH_DISCONNECTED:       return "DISCONNECTED";
        case SSH_CONNECTING:         return "CONNECTING";
        case SSH_HANDSHAKING:        return "HANDSHAKING";
        case SSH_PROMPTING_USERNAME: return "PROMPTING_USERNAME";
        case SSH_PROMPTING_PASSWORD: return "PROMPTING_PASSWORD";
        case SSH_AUTHENTICATING:     return "AUTHENTICATING";
        case SSH_CHANNEL_OPENING:    return "CHANNEL_OPENING";
        case SSH_PTY_REQUESTING:     return "PTY_REQUESTING";
        case SSH_SHELL_REQUESTING:   return "SHELL_REQUESTING";
        case SSH_READY:              return "READY";
        default: return "UNKNOWN";
    }
}
