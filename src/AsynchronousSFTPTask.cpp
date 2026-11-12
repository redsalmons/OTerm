#include "AsynchronousSFTPTask.h"
#include "SSHManager.h"
#include "GlobalConfig.h"
#include "TranslationHelper.h"
#include <wx/app.h>
#include <wx/filename.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <iostream>
#include <thread>
#include <cerrno>
#include <cstring>
#include <ctime>

wxDEFINE_EVENT(wxEVT_FILE_TRANSFER_PROGRESS, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_FILE_TRANSFER_COMPLETE, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_SSH_FILE_LIST, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_SSH_COMMAND_OUTPUT, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_SFTP_DNS_RESOLVED, wxCommandEvent);

// Initialize static members
std::vector<AsynchronousSFTPTask*> AsynchronousSFTPTask::s_activeTasks;
std::queue<AsynchronousSFTPTask*> AsynchronousSFTPTask::s_pendingTasks;

const char* AsynchronousSFTPTask::StateName(SFTPState state) {
    switch (state) {
        case SFTPState::CONNECTING:     return "CONNECTING";
        case SFTPState::HANDSHAKE:      return "HANDSHAKE";
        case SFTPState::AUTHENTICATING: return "AUTHENTICATING";
        case SFTPState::SFTP_INIT:      return "SFTP_INIT";
        case SFTPState::SCP_OPEN:       return "SCP_OPEN";
        case SFTPState::OPEN_CHANNEL:   return "OPEN_CHANNEL";
        case SFTPState::EXEC_CMD:       return "EXEC_CMD";
        case SFTPState::READ_CMD:       return "READ_CMD";
        case SFTPState::CLOSE_CHANNEL:  return "CLOSE_CHANNEL";
        case SFTPState::OPEN_FILE:      return "OPEN_FILE";
        case SFTPState::TRANSFERRING:   return "TRANSFERRING";
        case SFTPState::SCP_CLOSE:      return "SCP_CLOSE";
        case SFTPState::CLOSE_FILE:     return "CLOSE_FILE";
        case SFTPState::CLEANUP:        return "CLEANUP";
        case SFTPState::FINISHED:       return "FINISHED";
        case SFTPState::FAILED:         return "FAILED";
        default:                        return "UNKNOWN";
    }
}

const char* AsynchronousSFTPTask::SftpFxErrorName(unsigned long fxErr) {
    switch (fxErr) {
        case LIBSSH2_FX_OK:                       return "LIBSSH2_FX_OK";
        case LIBSSH2_FX_EOF:                      return "LIBSSH2_FX_EOF";
        case LIBSSH2_FX_NO_SUCH_FILE:             return "LIBSSH2_FX_NO_SUCH_FILE";
        case LIBSSH2_FX_PERMISSION_DENIED:        return "LIBSSH2_FX_PERMISSION_DENIED";
        case LIBSSH2_FX_FAILURE:                  return "LIBSSH2_FX_FAILURE";
        case LIBSSH2_FX_BAD_MESSAGE:              return "LIBSSH2_FX_BAD_MESSAGE";
        case LIBSSH2_FX_NO_CONNECTION:            return "LIBSSH2_FX_NO_CONNECTION";
        case LIBSSH2_FX_CONNECTION_LOST:          return "LIBSSH2_FX_CONNECTION_LOST";
        case LIBSSH2_FX_OP_UNSUPPORTED:           return "LIBSSH2_FX_OP_UNSUPPORTED";
        case LIBSSH2_FX_INVALID_HANDLE:           return "LIBSSH2_FX_INVALID_HANDLE";
        case LIBSSH2_FX_NO_SUCH_PATH:             return "LIBSSH2_FX_NO_SUCH_PATH";
        case LIBSSH2_FX_FILE_ALREADY_EXISTS:      return "LIBSSH2_FX_FILE_ALREADY_EXISTS";
        case LIBSSH2_FX_WRITE_PROTECT:            return "LIBSSH2_FX_WRITE_PROTECT";
        case LIBSSH2_FX_NO_MEDIA:                 return "LIBSSH2_FX_NO_MEDIA";
        case LIBSSH2_FX_NO_SPACE_ON_FILESYSTEM:   return "LIBSSH2_FX_NO_SPACE_ON_FILESYSTEM";
        case LIBSSH2_FX_QUOTA_EXCEEDED:           return "LIBSSH2_FX_QUOTA_EXCEEDED";
        case LIBSSH2_FX_UNKNOWN_PRINCIPAL:        return "LIBSSH2_FX_UNKNOWN_PRINCIPAL";
        case LIBSSH2_FX_LOCK_CONFlICT:            return "LIBSSH2_FX_LOCK_CONFLICT";
        case LIBSSH2_FX_DIR_NOT_EMPTY:            return "LIBSSH2_FX_DIR_NOT_EMPTY";
        case LIBSSH2_FX_NOT_A_DIRECTORY:          return "LIBSSH2_FX_NOT_A_DIRECTORY";
        case LIBSSH2_FX_INVALID_FILENAME:         return "LIBSSH2_FX_INVALID_FILENAME";
        case LIBSSH2_FX_LINK_LOOP:                return "LIBSSH2_FX_LINK_LOOP";
        default:                                  return "LIBSSH2_FX_UNKNOWN";
    }
}

std::string AsynchronousSFTPTask::TaskTag() const {
    std::ostringstream oss;
    oss << "[SFTP#" << static_cast<const void*>(this) << " ";
    if (m_isList) {
        oss << "list path='" << m_remotePath.ToStdString() << "'";
    } else if (m_isCommand) {
        oss << "cmd='" << m_command.ToStdString() << "'";
    } else {
        oss << m_task.action << " id=" << m_task.id;
    }
    oss << " " << m_deviceConfig.username << "@" << m_deviceConfig.address
        << ":" << (m_deviceConfig.port.empty() ? "22" : m_deviceConfig.port) << "]";
    return oss.str();
}

std::string AsynchronousSFTPTask::FormatSessionError() const {
    std::ostringstream oss;
    if (m_sshSession) {
        char* errMsg = nullptr;
        int errLen = 0;
        int errNo = libssh2_session_last_error(m_sshSession, &errMsg, &errLen, 0);
        oss << "ssh_errno=" << errNo;
        if (errMsg && errLen > 0) {
            oss << " ssh_err='" << std::string(errMsg, errLen) << "'";
        }
    }
    if (m_sftpSession) {
        unsigned long fxErr = libssh2_sftp_last_error(m_sftpSession);
        oss << " sftp_fx=" << fxErr << "(" << SftpFxErrorName(fxErr) << ")";
    }
    if (m_socket && m_socket->Error()) {
        oss << " sock_err=" << static_cast<int>(m_socket->LastError());
    }
    return oss.str();
}

AsynchronousSFTPTask::AsynchronousSFTPTask(wxEvtHandler* handler, const DeviceConfig& deviceConfig)
    : m_handler(handler),
      m_deviceConfig(deviceConfig),
      m_isList(false),
      m_isCommand(false),
      m_state(SFTPState::CONNECTING),
      m_socket(nullptr),
      m_sshSession(nullptr),
      m_sftpSession(nullptr),
      m_sftpHandle(nullptr),
      m_sshChannel(nullptr),
      m_localFile(nullptr),
      m_bytesTransferred(0),
      m_timer(this, TIMER_ID),
      m_timeoutTimer(this, TIMEOUT_TIMER_ID) {

    Bind(wxEVT_TIMER, &AsynchronousSFTPTask::OnTimer, this, TIMER_ID);
    Bind(wxEVT_TIMER, &AsynchronousSFTPTask::OnTimeout, this, TIMEOUT_TIMER_ID);
    Bind(wxEVT_SFTP_DNS_RESOLVED, &AsynchronousSFTPTask::OnDnsResolved, this);
}

AsynchronousSFTPTask::~AsynchronousSFTPTask() {
    m_alive->store(false);
    m_timer.Stop();
    m_timeoutTimer.Stop();
    Cleanup();
}

AsynchronousSFTPTask* AsynchronousSFTPTask::CreateListTask(wxEvtHandler* handler, const DeviceConfig& deviceConfig, const wxString& path) {
    AsynchronousSFTPTask* task = new AsynchronousSFTPTask(handler, deviceConfig);
    task->m_remotePath = path;
    task->m_isList = true;
    SSH_LOG(task->TaskTag() << " CreateListTask created");
    task->Start();
    return task;
}

AsynchronousSFTPTask* AsynchronousSFTPTask::CreateCommandTask(wxEvtHandler* handler, const DeviceConfig& deviceConfig, const wxString& command) {
    AsynchronousSFTPTask* task = new AsynchronousSFTPTask(handler, deviceConfig);
    task->m_command = command;
    task->m_isCommand = true;
    SSH_LOG(task->TaskTag() << " CreateCommandTask created");
    task->Start();
    return task;
}

void AsynchronousSFTPTask::QueueTransferTask(wxEvtHandler* handler, const DeviceConfig& deviceConfig, const FileTransferTask& taskInfo) {
    AsynchronousSFTPTask* task = new AsynchronousSFTPTask(handler, deviceConfig);
    task->m_task = taskInfo;
    SSH_LOG(task->TaskTag() << " QueueTransferTask local='" << taskInfo.local
            << "' remote='" << taskInfo.remote << "' size=" << taskInfo.size
            << " pendingQueue=" << s_pendingTasks.size() << " activeTasks=" << s_activeTasks.size());
    
    // Put into pending queue first
    s_pendingTasks.push(task);
    
    // Update task UI status to pending
    wxCommandEvent progressEvt(wxEVT_FILE_TRANSFER_PROGRESS);
    nlohmann::json pj;
    pj["id"] = taskInfo.id;
    pj["progress"] = 0;
    pj["status"] = "pending";
    progressEvt.SetString(pj.dump().c_str());
    wxQueueEvent(handler, progressEvt.Clone());

    // Trigger queue management
    ManageQueue();
}

void AsynchronousSFTPTask::Start() {
    SSH_LOG(TaskTag() << " Start() auth_method='" << m_deviceConfig.auth_method
            << "' username='" << m_deviceConfig.username
            << "' password_len=" << m_deviceConfig.password.length()
            << " device_id='" << m_deviceConfig.id << "'");

    if (!EnsureLibssh2Init()) {
        Fail("Failed to initialize libssh2");
        return;
    }

    m_state = SFTPState::CONNECTING;
    m_socket = new wxSocketClient(wxSOCKET_NOWAIT);
    m_socket->SetEventHandler(*this, SOCKET_ID);
    m_socket->SetNotify(wxSOCKET_CONNECTION_FLAG | wxSOCKET_LOST_FLAG);
    m_socket->Notify(true);

    Bind(wxEVT_SOCKET, &AsynchronousSFTPTask::OnSocketEvent, this, SOCKET_ID);

    m_sshSession = libssh2_session_init();
    if (!m_sshSession) {
        Fail("Failed to initialize libssh2 session");
        return;
    }

    // Arm the stall watchdog; DoStateMachineStep re-arms it on progress.
    m_timeoutTimer.Start(STALL_TIMEOUT_MS, wxTIMER_ONE_SHOT);

    if (IsNumericIPv4(m_deviceConfig.address)) {
        ConnectSocket(m_deviceConfig.address);
    } else {
        // Resolve on a worker thread so DNS cannot stall the UI
        SSH_LOG(TaskTag() << " Resolving hostname asynchronously: " << m_deviceConfig.address);
        std::string host = m_deviceConfig.address;
        auto alive = m_alive;
        std::thread([this, host, alive]() {
            std::string ip = ResolveIPv4(host);
            if (!alive->load()) return;
            wxCommandEvent evt(wxEVT_SFTP_DNS_RESOLVED);
            evt.SetString(wxString::FromUTF8(ip));
            wxQueueEvent(this, evt.Clone());
        }).detach();
    }
}

void AsynchronousSFTPTask::ConnectSocket(const std::string& ip) {
    if (!m_socket) return;

    wxIPV4address addr;
    addr.Hostname(ip); // numeric IP — does not block

    long portLong = 22;
    if (!m_deviceConfig.port.empty()) {
        try {
            portLong = std::stol(m_deviceConfig.port);
        } catch (...) {
            portLong = 22;
        }
    }
    addr.Service(portLong);

    SSH_LOG(TaskTag() << " ConnectSocket non-blocking connect to " << ip << ":" << portLong);
    m_socket->Connect(addr, false); // non-blocking connect

#ifdef _WIN32
    // Start the polling loop (15ms tick) now that connect is in flight
    m_timer.Start(15);
#endif
}

void AsynchronousSFTPTask::OnDnsResolved(wxCommandEvent& event) {
    if (!m_socket || m_state != SFTPState::CONNECTING) return;

    std::string ip = event.GetString().ToStdString();
    SSH_LOG(TaskTag() << " DNS resolved '" << m_deviceConfig.address << "' -> '" << ip << "'");
    if (ip.empty()) {
        Fail("Could not resolve host: " + m_deviceConfig.address);
        return;
    }

    m_timeoutTimer.Start(STALL_TIMEOUT_MS, wxTIMER_ONE_SHOT);
    ConnectSocket(ip);
}

void AsynchronousSFTPTask::OnTimeout(wxTimerEvent& event) {
    if (!IsFinished()) {
        SSH_ERR(TaskTag() << " Watchdog timeout in state=" << StateName(m_state)
                << " bytesTransferred=" << m_bytesTransferred
                << " cmdOutputSize=" << m_cmdOutput.size()
                << " " << FormatSessionError());
        Fail("Operation timed out in state " + std::string(StateName(m_state)) + " (no progress)");
    }
}

void AsynchronousSFTPTask::Cancel() {
    SSH_LOG(TaskTag() << " Cancel() called in state=" << StateName(m_state));
    Fail("Transfer cancelled");
}

bool AsynchronousSFTPTask::IsFinished() const {
    return m_state == SFTPState::FINISHED || m_state == SFTPState::FAILED;
}

bool AsynchronousSFTPTask::IsRunning() const {
    return m_state != SFTPState::FINISHED && m_state != SFTPState::FAILED && m_state != SFTPState::CONNECTING;
}

void AsynchronousSFTPTask::OnTimer(wxTimerEvent& event) {
    DoStateMachineStep();
}

void AsynchronousSFTPTask::OnSocketEvent(wxSocketEvent& event) {
    if (!m_socket) return;
    
    switch (event.GetSocketEvent()) {
        case wxSOCKET_CONNECTION: {
            SSH_LOG(TaskTag() << " OnSocketEvent: wxSOCKET_CONNECTION established (sockfd=" << m_socket->GetSocket() << "), entering HANDSHAKE");
            m_state = SFTPState::HANDSHAKE;
#ifndef _WIN32
            // Move to wxEventLoopSourceHandler for pure OS events.
            // If FD source registration fails, fall back to the polling timer.
            m_socket->Notify(false);
            wxEventLoopBase* loop = wxEventLoop::GetActive();
            if (loop && m_socket->GetSocket() >= 0) {
                m_fdSource = loop->AddSourceForFD(m_socket->GetSocket(), this, wxEVENT_SOURCE_INPUT | wxEVENT_SOURCE_OUTPUT);
            }
            if (!m_fdSource) {
                m_timer.Start(15);
            }
#else
            m_timer.Start(15);
#endif
            DoStateMachineStep();
            break;
        }
        case wxSOCKET_LOST: {
            SSH_ERR(TaskTag() << " OnSocketEvent: wxSOCKET_LOST in state=" << StateName(m_state)
                    << " bytesTransferred=" << m_bytesTransferred << " " << FormatSessionError());
            Fail("Connection lost unexpectedly");
            break;
        }
        default:
            break;
    }
}

void AsynchronousSFTPTask::HandleDirections() {
#ifndef _WIN32
    if (m_fdSource) {
        if (!m_socket || !m_sshSession) return;
        int sockfd = m_socket->GetSocket();
        if (sockfd < 0) return;

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
    }
#endif
}

#ifndef _WIN32
void AsynchronousSFTPTask::OnReadWaiting() {
    DoStateMachineStep();
}

void AsynchronousSFTPTask::OnWriteWaiting() {
    DoStateMachineStep();
}

void AsynchronousSFTPTask::OnExceptionWaiting() {
    Fail("Socket exception / connection lost");
}
#endif

void AsynchronousSFTPTask::DoStateMachineStep() {
    if (IsFinished()) {
        m_timer.Stop();
        m_timeoutTimer.Stop();
        return;
    }

    // Reset the stall watchdog only on real progress (state transition or
    // bytes moved) — a bare poll tick must not keep it alive.
    long long progress = m_bytesTransferred + (long long)m_cmdOutput.size();
    if (m_state != m_watchState || progress != m_watchProgress) {
        m_watchState = m_state;
        m_watchProgress = progress;
        m_timeoutTimer.Start(STALL_TIMEOUT_MS, wxTIMER_ONE_SHOT);
    }

    switch (m_state) {
        case SFTPState::CONNECTING: {
            if (m_socket->IsConnected()) {
                SSH_LOG(TaskTag() << " CONNECTING -> HANDSHAKE (IsConnected=true, sockfd=" << m_socket->GetSocket() << ")");
                m_state = SFTPState::HANDSHAKE;
                DoStateMachineStep();
            } else if (m_socket->WaitOnConnect(0, 0)) {
                if (m_socket->IsConnected()) {
                    SSH_LOG(TaskTag() << " CONNECTING -> HANDSHAKE (WaitOnConnect=true, IsConnected=true, sockfd=" << m_socket->GetSocket() << ")");
                    m_state = SFTPState::HANDSHAKE;
                    DoStateMachineStep();
                } else {
                    SSH_ERR(TaskTag() << " WaitOnConnect completed but IsConnected=false, sockLastError=" << static_cast<int>(m_socket->LastError()));
                    Fail("Failed to connect to SSH server " + m_deviceConfig.address);
                }
            } else if (m_socket->Error()) {
                wxSocketError sockErr = m_socket->LastError();
                if (sockErr != wxSOCKET_WOULDBLOCK) {
                    SSH_ERR(TaskTag() << " Socket error during CONNECTING: sockLastError=" << static_cast<int>(sockErr));
                    Fail("Failed to connect to SSH server " + m_deviceConfig.address);
                }
            }
            break;
        }

        case SFTPState::HANDSHAKE: {
            libssh2_session_set_blocking(m_sshSession, 0);
            int sockfd = m_socket->GetSocket();
            int rc = libssh2_session_handshake(m_sshSession, sockfd);
            if (rc == 0) {
                const char* banner = libssh2_session_banner_get(m_sshSession);
                const char* kex = libssh2_session_methods(m_sshSession, LIBSSH2_METHOD_KEX);
                const char* hostkey = libssh2_session_methods(m_sshSession, LIBSSH2_METHOD_HOSTKEY);
                const char* cryptCs = libssh2_session_methods(m_sshSession, LIBSSH2_METHOD_CRYPT_CS);
                const char* macCs = libssh2_session_methods(m_sshSession, LIBSSH2_METHOD_MAC_CS);
                SSH_LOG(TaskTag() << " SSH handshake succeeded: banner='" << (banner ? banner : "")
                        << "' kex='" << (kex ? kex : "")
                        << "' hostkey='" << (hostkey ? hostkey : "")
                        << "' cipher='" << (cryptCs ? cryptCs : "")
                        << "' mac='" << (macCs ? macCs : "") << "' -> AUTHENTICATING");
                m_state = SFTPState::AUTHENTICATING;
                DoStateMachineStep();
            } else if (rc == LIBSSH2_ERROR_EAGAIN) {
                HandleDirections();
            } else {
                SSH_ERR(TaskTag() << " SSH handshake failed: rc=" << rc << " " << FormatSessionError());
                Fail("SSH handshake failed: error " + std::to_string(rc));
            }
            break;
        }

        case SFTPState::AUTHENTICATING: {
            if (!m_loggedAuthMethods) {
                const char* authList = libssh2_userauth_list(
                    m_sshSession,
                    m_deviceConfig.username.c_str(),
                    static_cast<unsigned int>(m_deviceConfig.username.length())
                );
                if (authList) {
                    SSH_LOG(TaskTag() << " Server supported auth methods for '" << m_deviceConfig.username << "': [" << authList << "]");
                    m_loggedAuthMethods = true;
                } else if (libssh2_session_last_errno(m_sshSession) == LIBSSH2_ERROR_EAGAIN) {
                    HandleDirections();
                    break;
                } else {
                    if (libssh2_userauth_authenticated(m_sshSession)) {
                        SSH_LOG(TaskTag() << " Session already authenticated via userauth_list (none auth)");
                    } else {
                        SSH_LOG(TaskTag() << " libssh2_userauth_list returned null (" << FormatSessionError() << "), proceeding to auth");
                    }
                    m_loggedAuthMethods = true;
                }
            }

            int rc = 0;
            if (m_deviceConfig.auth_method == "key") {
                // Re-sanitize at use time: keys stored before sanitization
                // existed may have padded/mangled lines.
                std::string keyData = DeviceConfig::SanitizeKeyMaterial(m_deviceConfig.password);
                rc = libssh2_userauth_publickey_frommemory(
                    m_sshSession,
                    m_deviceConfig.username.c_str(), m_deviceConfig.username.length(),
                    nullptr, 0,
                    keyData.c_str(), keyData.length(),
                    nullptr
                );
            } else {
                rc = libssh2_userauth_password(m_sshSession, m_deviceConfig.username.c_str(), m_deviceConfig.password.c_str());
            }
            if (rc == 0) {
                if (m_isList || m_isCommand) {
                    SSH_LOG(TaskTag() << " Authentication succeeded -> OPEN_CHANNEL");
                    m_state = SFTPState::OPEN_CHANNEL;
                } else {
                    SSH_LOG(TaskTag() << " Authentication succeeded -> SFTP_INIT");
                    m_state = SFTPState::SFTP_INIT;
                }
                DoStateMachineStep();
            } else if (rc == LIBSSH2_ERROR_EAGAIN) {
                HandleDirections();
            } else {
                SSH_ERR(TaskTag() << " Authentication failed: rc=" << rc
                        << " method='" << m_deviceConfig.auth_method
                        << "' user='" << m_deviceConfig.username
                        << "' password_len=" << m_deviceConfig.password.length()
                        << " " << FormatSessionError());
                Fail("Authentication failed for user " + m_deviceConfig.username);
            }
            break;
        }

        case SFTPState::SFTP_INIT: {
            m_sftpSession = libssh2_sftp_init(m_sshSession);
            if (m_sftpSession) {
                SSH_LOG(TaskTag() << " SFTP subsystem initialized -> OPEN_FILE");
                m_state = SFTPState::OPEN_FILE;
                DoStateMachineStep();
            } else {
                int err = libssh2_session_last_errno(m_sshSession);
                if (err == LIBSSH2_ERROR_EAGAIN) {
                    HandleDirections();
                } else {
                    // Server has no sftp subsystem (BusyBox/Dropbear etc.) —
                    // fall back to the SCP protocol like WinSCP does.
                    SSH_ERR(TaskTag() << " SFTP initialization failed (" << FormatSessionError()
                            << "), falling back to SCP");
                    m_useScp = true;
                    m_state = SFTPState::SCP_OPEN;
                    DoStateMachineStep();
                }
            }
            break;
        }

        case SFTPState::SCP_OPEN: {
            if (m_task.action == "upload") {
                // Guarded so EAGAIN retries of scp_send64 do not leak FILE*
                if (!m_localFile) {
#ifdef _WIN32
                    wxString wxLocal = wxString::FromUTF8(m_task.local.c_str());
                    m_localFile = _wfopen(wxLocal.wc_str(), L"rb");
#else
                    m_localFile = fopen(m_task.local.c_str(), "rb");
#endif
                    if (!m_localFile) {
                        SSH_ERR(TaskTag() << " SCP: failed to open local file for reading: '" << m_task.local
                                << "' errno=" << errno << " (" << std::strerror(errno) << ")");
                        Fail("Failed to open local file for reading: " + m_task.local);
                        break;
                    }
                }
                wxFileName fn(wxString::FromUTF8(m_task.local.c_str()));
                // scp_send64 must get the exact byte count — always use the
                // real file size, the queued value may be stale or zero.
                wxULongLong sz = fn.GetSize();
                if (sz != wxInvalidSize) m_task.size = static_cast<long long>(sz.GetValue());
                wxDateTime dt = fn.GetModificationTime();
                time_t mtime = dt.IsValid() ? static_cast<time_t>(dt.GetTicks()) : time(nullptr);
                m_sshChannel = libssh2_scp_send64(m_sshSession, m_task.remote.c_str(), 0644,
                                                m_task.size, mtime, mtime);
                if (m_sshChannel) {
                    SSH_LOG(TaskTag() << " SCP send channel opened: remote='" << m_task.remote
                            << "' size=" << m_task.size << " -> TRANSFERRING");
                    m_lastReportedProgress = 0;
                    wxCommandEvent progressEvt(wxEVT_FILE_TRANSFER_PROGRESS);
                    nlohmann::json pj;
                    pj["id"] = m_task.id;
                    pj["progress"] = 0;
                    pj["status"] = "processing";
                    progressEvt.SetString(pj.dump().c_str());
                    wxQueueEvent(m_handler, progressEvt.Clone());
                    m_state = SFTPState::TRANSFERRING;
                    DoStateMachineStep();
                } else {
                    int err = libssh2_session_last_errno(m_sshSession);
                    if (err == LIBSSH2_ERROR_EAGAIN) {
                        HandleDirections();
                    } else {
                        SSH_ERR(TaskTag() << " SCP send open failed: remote='" << m_task.remote
                                << "' " << FormatSessionError());
                        Fail("Failed to open remote file for writing (SCP): " + m_task.remote);
                    }
                }
            } else { // download
                m_sshChannel = libssh2_scp_recv2(m_sshSession, m_task.remote.c_str(), &m_scpStat);
                if (m_sshChannel) {
                    if (m_scpStat.st_size >= 0) {
                        m_task.size = static_cast<long long>(m_scpStat.st_size);
                    }
                    SSH_LOG(TaskTag() << " SCP recv channel opened: remote='" << m_task.remote
                            << "' size=" << m_task.size);
#ifdef _WIN32
                    wxString wxLocal = wxString::FromUTF8(m_task.local.c_str());
                    m_localFile = _wfopen(wxLocal.wc_str(), L"wb");
#else
                    m_localFile = fopen(m_task.local.c_str(), "wb");
#endif
                    if (!m_localFile) {
                        SSH_ERR(TaskTag() << " SCP: failed to open local file for writing: '" << m_task.local
                                << "' errno=" << errno << " (" << std::strerror(errno) << ")");
                        Fail("Failed to open local file for writing: " + m_task.local);
                        break;
                    }
                    m_lastReportedProgress = 0;
                    wxCommandEvent progressEvt(wxEVT_FILE_TRANSFER_PROGRESS);
                    nlohmann::json pj;
                    pj["id"] = m_task.id;
                    pj["progress"] = 0;
                    pj["status"] = "processing";
                    progressEvt.SetString(pj.dump().c_str());
                    wxQueueEvent(m_handler, progressEvt.Clone());
                    m_state = SFTPState::TRANSFERRING;
                    DoStateMachineStep();
                } else {
                    int err = libssh2_session_last_errno(m_sshSession);
                    if (err == LIBSSH2_ERROR_EAGAIN) {
                        HandleDirections();
                    } else {
                        SSH_ERR(TaskTag() << " SCP recv open failed: remote='" << m_task.remote
                                << "' " << FormatSessionError());
                        Fail("Failed to open remote file for reading (SCP): " + m_task.remote);
                    }
                }
            }
            break;
        }

        case SFTPState::OPEN_CHANNEL: {
            m_sshChannel = libssh2_channel_open_session(m_sshSession);
            if (m_sshChannel) {
                SSH_LOG(TaskTag() << " SSH channel opened -> EXEC_CMD");
                m_state = SFTPState::EXEC_CMD;
                DoStateMachineStep();
            } else {
                int err = libssh2_session_last_errno(m_sshSession);
                if (err == LIBSSH2_ERROR_EAGAIN) {
                    HandleDirections();
                } else {
                    SSH_ERR(TaskTag() << " Failed to open SSH session channel: " << FormatSessionError());
                    Fail("Failed to open SSH session channel");
                }
            }
            break;
        }

        case SFTPState::EXEC_CMD: {
            std::string cmd = m_isList ? ("LC_ALL=C ls -la " + m_remotePath.ToStdString()) : m_command.ToStdString();
            int rc = libssh2_channel_exec(m_sshChannel, cmd.c_str());
            if (rc == 0) {
                SSH_LOG(TaskTag() << " Executing remote command: '" << cmd << "' -> READ_CMD");
                m_state = SFTPState::READ_CMD;
                DoStateMachineStep();
            } else if (rc == LIBSSH2_ERROR_EAGAIN) {
                HandleDirections();
            } else {
                SSH_ERR(TaskTag() << " Failed to execute command '" << cmd << "': rc=" << rc << " " << FormatSessionError());
                Fail("Failed to execute command: " + cmd);
            }
            break;
        }

        case SFTPState::READ_CMD: {
            char buffer[4096];
            while (true) {
                // Drain stderr first so its window cannot stall the channel.
                // For command tasks the text is kept; for `ls` output it would
                // corrupt the listing, so it is logged separately.
                while (true) {
                    int src = libssh2_channel_read_stderr(m_sshChannel, buffer, sizeof(buffer));
                    if (src <= 0) break;
                    m_cmdStderr.append(buffer, (size_t)src);
                    if (m_isCommand) {
                        m_cmdOutput.append(buffer, (size_t)src);
                    }
                }

                int rc = libssh2_channel_read(m_sshChannel, buffer, sizeof(buffer));
                if (rc > 0) {
                    m_cmdOutput.append(buffer, rc);
                } else if (rc == LIBSSH2_ERROR_EAGAIN) {
                    HandleDirections();
                    break; // wait for next tick
                } else if (rc == 0) {
                    SSH_LOG(TaskTag() << " READ_CMD EOF: stdout_bytes=" << m_cmdOutput.size()
                            << " stderr_bytes=" << m_cmdStderr.size()
                            << (m_cmdStderr.empty() ? "" : (" stderr='" + m_cmdStderr + "'"))
                            << " -> CLOSE_CHANNEL");
                    m_state = SFTPState::CLOSE_CHANNEL;
                    DoStateMachineStep();
                    break;
                } else {
                    SSH_ERR(TaskTag() << " Error reading command output: rc=" << rc << " " << FormatSessionError());
                    Fail("Error reading command execution output");
                    break;
                }
            }
            break;
        }

        case SFTPState::CLOSE_CHANNEL: {
            int rc = libssh2_channel_close(m_sshChannel);
            if (rc == 0 || rc != LIBSSH2_ERROR_EAGAIN) {
                int exitStatus = libssh2_channel_get_exit_status(m_sshChannel);
                SSH_LOG(TaskTag() << " Channel closed (rc=" << rc << ", exit_status=" << exitStatus << ") -> CLEANUP");
                libssh2_channel_free(m_sshChannel);
                m_sshChannel = nullptr;
                m_state = SFTPState::CLEANUP;
                DoStateMachineStep();
            } else if (rc == LIBSSH2_ERROR_EAGAIN) {
                HandleDirections();
            }
            break;
        }

        case SFTPState::OPEN_FILE: {
            if (m_task.action == "upload") {
                m_sftpHandle = libssh2_sftp_open_ex(
                    m_sftpSession, m_task.remote.c_str(), m_task.remote.length(),
                    LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC,
                    LIBSSH2_SFTP_S_IRUSR | LIBSSH2_SFTP_S_IWUSR | LIBSSH2_SFTP_S_IRGRP | LIBSSH2_SFTP_S_IROTH,
                    LIBSSH2_SFTP_OPENFILE
                );
                if (m_sftpHandle) {
                    SSH_LOG(TaskTag() << " Remote file opened for upload: '" << m_task.remote
                            << "', opening local file '" << m_task.local << "' (expected size=" << m_task.size << ")");
#ifdef _WIN32
                    wxString wxLocal = wxString::FromUTF8(m_task.local.c_str());
                    m_localFile = _wfopen(wxLocal.wc_str(), L"rb");
#else
                    m_localFile = fopen(m_task.local.c_str(), "rb");
#endif
                    if (!m_localFile) {
                        SSH_ERR(TaskTag() << " Failed to open local file for reading: '" << m_task.local
                                << "' errno=" << errno << " (" << std::strerror(errno) << ")");
                        Fail("Failed to open local file for reading: " + m_task.local);
                    } else {
                        m_lastReportedProgress = 0;
                        wxCommandEvent progressEvt(wxEVT_FILE_TRANSFER_PROGRESS);
                        nlohmann::json pj;
                        pj["id"] = m_task.id;
                        pj["progress"] = 0;
                        pj["status"] = "processing";
                        progressEvt.SetString(pj.dump().c_str());
                        wxQueueEvent(m_handler, progressEvt.Clone());

                        m_state = SFTPState::TRANSFERRING;
                        DoStateMachineStep();
                    }
                } else {
                    int err = libssh2_session_last_errno(m_sshSession);
                    if (err == LIBSSH2_ERROR_EAGAIN) {
                        HandleDirections();
                    } else {
                        SSH_ERR(TaskTag() << " Failed to open remote file for writing: '" << m_task.remote
                                << "' " << FormatSessionError());
                        Fail("Failed to open remote file for writing: " + m_task.remote);
                    }
                }
            } else { // download
                m_sftpHandle = libssh2_sftp_open_ex(
                    m_sftpSession, m_task.remote.c_str(), m_task.remote.length(),
                    LIBSSH2_FXF_READ, 0, LIBSSH2_SFTP_OPENFILE
                );
                if (m_sftpHandle) {
                    SSH_LOG(TaskTag() << " Remote file opened for download: '" << m_task.remote
                            << "', opening local file '" << m_task.local << "' (expected size=" << m_task.size << ")");
#ifdef _WIN32
                    wxString wxLocal = wxString::FromUTF8(m_task.local.c_str());
                    m_localFile = _wfopen(wxLocal.wc_str(), L"wb");
#else
                    m_localFile = fopen(m_task.local.c_str(), "wb");
#endif
                    if (!m_localFile) {
                        SSH_ERR(TaskTag() << " Failed to open local file for writing: '" << m_task.local
                                << "' errno=" << errno << " (" << std::strerror(errno) << ")");
                        Fail("Failed to open local file for writing: " + m_task.local);
                    } else {
                        m_lastReportedProgress = 0;
                        wxCommandEvent progressEvt(wxEVT_FILE_TRANSFER_PROGRESS);
                        nlohmann::json pj;
                        pj["id"] = m_task.id;
                        pj["progress"] = 0;
                        pj["status"] = "processing";
                        progressEvt.SetString(pj.dump().c_str());
                        wxQueueEvent(m_handler, progressEvt.Clone());

                        m_state = SFTPState::TRANSFERRING;
                        DoStateMachineStep();
                    }
                } else {
                    int err = libssh2_session_last_errno(m_sshSession);
                    if (err == LIBSSH2_ERROR_EAGAIN) {
                        HandleDirections();
                    } else {
                        SSH_ERR(TaskTag() << " Failed to open remote file for reading: '" << m_task.remote
                                << "' " << FormatSessionError());
                        Fail("Failed to open remote file for reading: " + m_task.remote);
                    }
                }
            }
            break;
        }

        case SFTPState::TRANSFERRING: {
            char buffer[32768];
            bool blocked = false;
            // Bound this step to what the channel advertises on entry: drain
            // the bytes already buffered (download) / what fits the send
            // window (upload), then yield to the event loop instead of
            // looping until EAGAIN — which on fast links never comes.
            LIBSSH2_CHANNEL* chan = m_useScp
                ? m_sshChannel
                : libssh2_sftp_get_channel(m_sftpSession);
            long long budget = 0;
            if (chan) {
                if (m_task.action == "upload") {
                    budget = static_cast<long long>(
                        libssh2_channel_window_write_ex(chan, NULL));
                } else {
                    unsigned long avail = 0;
                    libssh2_channel_window_read_ex(chan, &avail, NULL);
                    budget = static_cast<long long>(avail);
                }
            }
            // Always attempt at least one I/O call: buffered packets not yet
            // counted in read_avail / window may still yield data.
            bool attempted = false;
            while (!blocked && (!attempted || budget > 0)) {
                attempted = true;
                if (m_task.action == "upload") {
                    if (m_cacheBuffer.empty()) {
                        size_t bytesRead = fread(buffer, 1, sizeof(buffer), m_localFile);
                        if (bytesRead > 0) {
                            m_cacheBuffer.assign(buffer, buffer + bytesRead);
                        } else {
                            if (ferror(m_localFile)) {
                                SSH_ERR(TaskTag() << " Local file fread error at offset=" << m_bytesTransferred
                                        << " errno=" << errno << " (" << std::strerror(errno) << ")");
                                Fail("Local file read error");
                                break;
                            }
                            // EOF local file
                            SSH_LOG(TaskTag() << " Upload local EOF reached, total bytesTransferred=" << m_bytesTransferred << " -> CLOSE_FILE");
                            m_state = SFTPState::CLOSE_FILE;
                            DoStateMachineStep();
                            break;
                        }
                    }
                    
                    size_t sendLen = m_cacheBuffer.size();
                    if (budget > 0 && sendLen > static_cast<size_t>(budget)) {
                        sendLen = static_cast<size_t>(budget);
                    }
                    int rc = m_useScp
                        ? static_cast<int>(libssh2_channel_write(m_sshChannel, m_cacheBuffer.data(), sendLen))
                        : static_cast<int>(libssh2_sftp_write(m_sftpHandle, m_cacheBuffer.data(), sendLen));
                    if (rc > 0) {
                        m_bytesTransferred += rc;
                        budget -= rc;
                        m_cacheBuffer.erase(m_cacheBuffer.begin(), m_cacheBuffer.begin() + rc);
                        
                        // Send progress event only when percentage changes
                        if (m_task.size > 0) {
                            int progress = static_cast<int>((m_bytesTransferred * 100) / m_task.size);
                            if (progress > 100) progress = 100;
                            if (progress != m_lastReportedProgress) {
                                if (progress == 1 || progress % 10 == 0 || progress == 100) {
                                    SSH_LOG(TaskTag() << " Upload progress: " << progress << "% (" << m_bytesTransferred << "/" << m_task.size << " bytes)");
                                }
                                m_lastReportedProgress = progress;
                                wxCommandEvent progressEvt(wxEVT_FILE_TRANSFER_PROGRESS);
                                nlohmann::json pj;
                                pj["id"] = m_task.id;
                                pj["progress"] = progress;
                                pj["status"] = "processing";
                                progressEvt.SetString(pj.dump().c_str());
                                wxQueueEvent(m_handler, progressEvt.Clone());
                            }
                        }
                    } else if (rc == LIBSSH2_ERROR_EAGAIN) {
                        HandleDirections();
                        blocked = true;
                    } else {
                        SSH_ERR(TaskTag() << " SFTP/SCP write failed at offset=" << m_bytesTransferred
                                << " chunk=" << m_cacheBuffer.size() << " rc=" << rc << " " << FormatSessionError());
                        Fail(m_useScp ? "SCP write failed" : "SFTP write failed");
                        blocked = true;
                    }
                } else { // download
                    if (m_useScp && m_bytesTransferred >= m_task.size) {
                        // scp -f does not close the channel after the file body —
                        // it waits for our final ack. Treat st_size bytes as EOF
                        // and stop before consuming the trailing status byte.
                        SSH_LOG(TaskTag() << " Download complete (SCP), total bytesTransferred=" << m_bytesTransferred << " -> CLOSE_FILE");
                        m_state = SFTPState::CLOSE_FILE;
                        DoStateMachineStep();
                        break;
                    }
                    size_t want = sizeof(buffer);
                    if (m_useScp) {
                        long long remaining = m_task.size - m_bytesTransferred;
                        if (remaining < static_cast<long long>(want)) {
                            want = static_cast<size_t>(remaining > 0 ? remaining : 0);
                        }
                    }
                    if (budget > 0 && static_cast<long long>(want) > budget) {
                        want = static_cast<size_t>(budget);
                    }
                    int rc = m_useScp
                        ? static_cast<int>(libssh2_channel_read(m_sshChannel, buffer, want))
                        : static_cast<int>(libssh2_sftp_read(m_sftpHandle, buffer, want));
                    if (rc > 0) {
                        budget -= rc;
                        size_t written = fwrite(buffer, 1, rc, m_localFile);
                        if (written < (size_t)rc) {
                            SSH_ERR(TaskTag() << " Local file fwrite failed: written=" << written << " expected=" << rc
                                    << " errno=" << errno << " (" << std::strerror(errno) << ")");
                            Fail("Local file write failed (disk full?)");
                            blocked = true;
                            break;
                        }
                        m_bytesTransferred += rc;
                        
                        // Send progress event only when percentage changes
                        if (m_task.size > 0) {
                            int progress = static_cast<int>((m_bytesTransferred * 100) / m_task.size);
                            if (progress > 100) progress = 100;
                            if (progress != m_lastReportedProgress) {
                                if (progress == 1 || progress % 10 == 0 || progress == 100) {
                                    SSH_LOG(TaskTag() << " Download progress: " << progress << "% (" << m_bytesTransferred << "/" << m_task.size << " bytes)");
                                }
                                m_lastReportedProgress = progress;
                                wxCommandEvent progressEvt(wxEVT_FILE_TRANSFER_PROGRESS);
                                nlohmann::json pj;
                                pj["id"] = m_task.id;
                                pj["progress"] = progress;
                                pj["status"] = "processing";
                                progressEvt.SetString(pj.dump().c_str());
                                wxQueueEvent(m_handler, progressEvt.Clone());
                            }
                        }
                    } else if (rc == 0) {
                        // EOF remote file
                        SSH_LOG(TaskTag() << " Download remote EOF reached, total bytesTransferred=" << m_bytesTransferred << " -> CLOSE_FILE");
                        m_state = SFTPState::CLOSE_FILE;
                        DoStateMachineStep();
                        break;
                    } else if (rc == LIBSSH2_ERROR_EAGAIN) {
                        HandleDirections();
                        blocked = true;
                    } else {
                        SSH_ERR(TaskTag() << " SFTP/SCP read failed at offset=" << m_bytesTransferred
                                << " rc=" << rc << " " << FormatSessionError());
                        Fail(m_useScp ? "SCP read failed" : "SFTP read failed");
                        blocked = true;
                    }
                }
            }
            if (!blocked && !IsFinished()) {
                // Exited on the step budget, not on EAGAIN — make sure the
                // 15 ms poll timer re-enters the state machine (on non-Windows
                // the FD source may not be watching the direction we need).
                if (!m_timer.IsRunning()) {
                    m_timer.Start(15);
                }
            }
            break;
        }

        case SFTPState::CLOSE_FILE: {
            if (m_localFile) {
                fclose(m_localFile);
                m_localFile = nullptr;
            }
            if (m_useScp) {
                m_state = SFTPState::SCP_CLOSE;
                m_scpCloseStep = 0;
                DoStateMachineStep();
                break;
            }
            int rc = libssh2_sftp_close(m_sftpHandle);
            if (rc == 0 || rc != LIBSSH2_ERROR_EAGAIN) {
                if (rc != 0) {
                    SSH_ERR(TaskTag() << " libssh2_sftp_close returned rc=" << rc << " " << FormatSessionError());
                } else {
                    SSH_LOG(TaskTag() << " SFTP file handle closed -> CLEANUP");
                }
                m_sftpHandle = nullptr;
                m_state = SFTPState::CLEANUP;
                DoStateMachineStep();
            } else if (rc == LIBSSH2_ERROR_EAGAIN) {
                HandleDirections();
            }
            break;
        }

        case SFTPState::SCP_CLOSE: {
            if (!m_sshChannel) {
                m_state = SFTPState::CLEANUP;
                DoStateMachineStep();
                break;
            }
            int rc = 0;
            if (m_scpCloseStep == 0) {
                if (m_task.action == "upload") {
                    rc = libssh2_channel_send_eof(m_sshChannel);
                } else {
                    // scp -f waits for a final '\0' ack after the file body
                    rc = static_cast<int>(libssh2_channel_write(m_sshChannel, "\0", 1));
                }
                if (rc == LIBSSH2_ERROR_EAGAIN) {
                    HandleDirections();
                    break;
                }
                m_scpCloseStep = (m_task.action == "upload") ? 1 : 2;
            }
            if (m_scpCloseStep == 1) {
                rc = libssh2_channel_wait_eof(m_sshChannel);
                if (rc == LIBSSH2_ERROR_EAGAIN) {
                    HandleDirections();
                    break;
                }
                m_scpCloseStep = 2;
            }
            if (m_scpCloseStep == 2) {
                rc = libssh2_channel_wait_closed(m_sshChannel);
                if (rc == LIBSSH2_ERROR_EAGAIN) {
                    HandleDirections();
                    break;
                }
                int exitStatus = libssh2_channel_get_exit_status(m_sshChannel);
                SSH_LOG(TaskTag() << " SCP channel closed (exit_status=" << exitStatus << ") -> CLEANUP");
                libssh2_channel_free(m_sshChannel);
                m_sshChannel = nullptr;
                m_state = SFTPState::CLEANUP;
                DoStateMachineStep();
            }
            break;
        }

        case SFTPState::CLEANUP: {
            if (m_sftpSession) {
                int rc = libssh2_sftp_shutdown(m_sftpSession);
                if (rc == LIBSSH2_ERROR_EAGAIN) {
                    HandleDirections();
                    return;
                }
                m_sftpSession = nullptr;
            }
            if (m_sshSession) {
                int rc = libssh2_session_disconnect(m_sshSession, "Normal shutdown");
                if (rc == LIBSSH2_ERROR_EAGAIN) {
                    HandleDirections();
                    return;
                }
                libssh2_session_free(m_sshSession);
                m_sshSession = nullptr;
            }
            m_state = SFTPState::FINISHED;
            Succeed();
            break;
        }

        default:
            break;
    }
}

void AsynchronousSFTPTask::Fail(const std::string& errorMsg) {
    SSH_ERR(TaskTag() << " Fail() in state=" << StateName(m_state)
            << " reason='" << errorMsg << "' bytesTransferred=" << m_bytesTransferred
            << " " << FormatSessionError());
    m_state = SFTPState::FAILED;
    m_timer.Stop();
    m_timeoutTimer.Stop();
    
    Cleanup();
    
    if (m_isList) {
        wxCommandEvent event(wxEVT_SSH_FILE_LIST);
        event.SetString("ERROR: " + errorMsg);
        wxQueueEvent(m_handler, event.Clone());
    } else if (m_isCommand) {
        wxCommandEvent event(wxEVT_SSH_COMMAND_OUTPUT);
        event.SetString("ERROR: " + errorMsg);
        wxQueueEvent(m_handler, event.Clone());
    } else {
        m_task.status = "failed";
        m_task.result = errorMsg;
        
        wxCommandEvent event(wxEVT_FILE_TRANSFER_COMPLETE);
        event.SetString(m_task.to_json().dump().c_str());
        wxQueueEvent(m_handler, event.Clone());
    }

    // Manage pending queue
    if (!m_isList && !m_isCommand) {
        ManageQueue();
    } else {
        // List/command tasks are not tracked in s_activeTasks — nobody else
        // owns this object, so schedule self-deletion.
        wxTheApp->CallAfter([this]() { delete this; });
    }
}

void AsynchronousSFTPTask::Succeed() {
    SSH_LOG(TaskTag() << " Succeed() bytesTransferred=" << m_bytesTransferred
            << " cmdOutputSize=" << m_cmdOutput.size());
    m_state = SFTPState::FINISHED;
    m_timer.Stop();
    m_timeoutTimer.Stop();
    
    Cleanup();
    
    if (m_isList) {
        wxCommandEvent event(wxEVT_SSH_FILE_LIST);
        wxString fullOutput = m_remotePath + "\n" + wxString(m_cmdOutput.c_str(), wxConvUTF8);
        event.SetString(fullOutput);
        wxQueueEvent(m_handler, event.Clone());
    } else if (m_isCommand) {
        wxCommandEvent event(wxEVT_SSH_COMMAND_OUTPUT);
        event.SetString(wxString(m_cmdOutput.c_str(), wxConvUTF8));
        wxQueueEvent(m_handler, event.Clone());
    } else {
        m_task.status = "completed";
        m_task.progress = 100;
        m_task.result = "success";

        wxCommandEvent event(wxEVT_FILE_TRANSFER_COMPLETE);
        event.SetString(m_task.to_json().dump().c_str());
        wxQueueEvent(m_handler, event.Clone());
    }

    // Manage pending queue
    if (!m_isList && !m_isCommand) {
        ManageQueue();
    } else {
        // List/command tasks are not tracked in s_activeTasks — nobody else
        // owns this object, so schedule self-deletion.
        wxTheApp->CallAfter([this]() { delete this; });
    }
}

void AsynchronousSFTPTask::Cleanup() {
#ifndef _WIN32
    if (m_fdSource) {
        delete m_fdSource;
        m_fdSource = nullptr;
    }
#endif

    if (m_localFile) {
        fclose(m_localFile);
        m_localFile = nullptr;
    }
    if (m_sshChannel) {
        libssh2_channel_free(m_sshChannel);
        m_sshChannel = nullptr;
    }
    if (m_sftpHandle) {
        libssh2_sftp_close(m_sftpHandle);
        m_sftpHandle = nullptr;
    }
    if (m_sftpSession) {
        libssh2_sftp_shutdown(m_sftpSession);
        m_sftpSession = nullptr;
    }
    if (m_sshSession) {
        libssh2_session_disconnect(m_sshSession, "Normal cleanup");
        libssh2_session_free(m_sshSession);
        m_sshSession = nullptr;
    }
    if (m_socket) {
        m_socket->Destroy();
        m_socket = nullptr;
    }
}

void AsynchronousSFTPTask::ManageQueue() {
    // Delete finished tasks in s_activeTasks
    auto it = std::remove_if(s_activeTasks.begin(), s_activeTasks.end(), [](AsynchronousSFTPTask* t) {
        if (t->IsFinished()) {
            wxTheApp->CallAfter([t]() { delete t; });
            return true;
        }
        return false;
    });
    s_activeTasks.erase(it, s_activeTasks.end());

    // Pull from s_pendingTasks and start them
    while (s_activeTasks.size() < MAX_ACTIVE_TRANSFERS && !s_pendingTasks.empty()) {
        AsynchronousSFTPTask* nextTask = s_pendingTasks.front();
        s_pendingTasks.pop();

        // A task may have been cancelled/failed while still queued
        if (nextTask->IsFinished()) {
            wxTheApp->CallAfter([nextTask]() { delete nextTask; });
            continue;
        }

        s_activeTasks.push_back(nextTask);

        // Start next task
        nextTask->Start();
    }
}

void AsynchronousSFTPTask::CleanAllActiveTasks() {
    for (auto* t : s_activeTasks) {
        delete t;
    }
    s_activeTasks.clear();
    
    while (!s_pendingTasks.empty()) {
        delete s_pendingTasks.front();
        s_pendingTasks.pop();
    }
}
