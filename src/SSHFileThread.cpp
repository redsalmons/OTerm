#include "SSHFileThread.h"
#include <wx/arrstr.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#define closesocket close
#define WSACleanup() ((void)0)
#define WSAStartup(version, data) 0
#define MAKEWORD(a, b) 0
typedef int WSADATA;
#endif
#include "SSHManager.h"

#ifndef LIBSSH2_SOCKET
#define LIBSSH2_SOCKET int
#endif

wxDEFINE_EVENT(wxEVT_SSH_FILE_LIST, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_SSH_COMMAND_OUTPUT, wxCommandEvent);

SSHFileThread::SSHFileThread(wxEvtHandler* handler, const DeviceConfig& deviceConfig)
    : wxThread(wxTHREAD_JOINABLE), m_handler(handler), m_deviceConfig(deviceConfig),
      m_sshSession(nullptr), m_sshChannel(nullptr), m_shouldStop(false), m_condition(m_mutex) {
}

SSHFileThread::~SSHFileThread() {
    Stop();
}

void SSHFileThread::RequestDirectory(const wxString& path) {
    wxMutexLocker lock(m_mutex);
    DirectoryRequest req;
    req.path = path;
    req.isCommand = false;
    m_requestQueue.push(req);
    m_condition.Signal();
}

void SSHFileThread::ExecuteCommand(const wxString& command) {
    wxMutexLocker lock(m_mutex);
    DirectoryRequest req;
    req.path = command;
    req.isCommand = true;
    m_requestQueue.push(req);
    m_condition.Signal();
}

void SSHFileThread::Stop() {
    {
        wxMutexLocker lock(m_mutex);
        m_shouldStop = true;
        m_condition.Signal();
    }
    
    if (IsAlive()) {
        Wait();
    }
}

bool SSHFileThread::ConnectSSH() {
    // Initialize Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return false;
    }
    
    // Create socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        WSACleanup();
        return false;
    }
    
    // Connect to server
    if (m_deviceConfig.port.empty()) {
        SSH_LOG("SSHFileThread: Port is empty, cannot connect");
        return false;
    }
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(std::stoi(m_deviceConfig.port));
    addr.sin_addr.s_addr = inet_addr(m_deviceConfig.address.c_str());
    
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        closesocket(sock);
        WSACleanup();
        return false;
    }
    
    // Initialize libssh2 session
    m_sshSession = libssh2_session_init();
    if (!m_sshSession) {
        closesocket(sock);
        WSACleanup();
        return false;
    }
    
    // Set blocking mode
    libssh2_session_set_blocking(m_sshSession, 1);
    
    // Start SSH session
    if (libssh2_session_handshake(m_sshSession, (LIBSSH2_SOCKET)sock) != 0) {
        libssh2_session_free(m_sshSession);
        closesocket(sock);
        WSACleanup();
        return false;
    }
    
    // Authenticate
    if (libssh2_userauth_password(m_sshSession, m_deviceConfig.username.c_str(),
                                   m_deviceConfig.password.c_str()) != 0) {
        libssh2_session_free(m_sshSession);
        closesocket(sock);
        WSACleanup();
        return false;
    }
    
    return true;
}

void SSHFileThread::DisconnectSSH() {
    if (m_sshChannel) {
        libssh2_channel_free(m_sshChannel);
        m_sshChannel = nullptr;
    }
    
    if (m_sshSession) {
        libssh2_session_disconnect(m_sshSession, "Normal shutdown");
        libssh2_session_free(m_sshSession);
        m_sshSession = nullptr;
    }
    
    WSACleanup();
}

bool SSHFileThread::ExecuteCommand(const std::string& command, std::string& output) {
    if (!m_sshSession) return false;
    
    // Open channel
    m_sshChannel = libssh2_channel_open_session(m_sshSession);
    if (!m_sshChannel) {
        return false;
    }
    
    // Execute command
    if (libssh2_channel_exec(m_sshChannel, command.c_str()) != 0) {
        libssh2_channel_free(m_sshChannel);
        m_sshChannel = nullptr;
        return false;
    }
    
    // Read output
    char buffer[4096];
    int bytes_read;
    output.clear();
    
    while ((bytes_read = libssh2_channel_read(m_sshChannel, buffer, sizeof(buffer))) > 0) {
        output.append(buffer, bytes_read);
    }
    
    // Close channel
    libssh2_channel_close(m_sshChannel);
    libssh2_channel_free(m_sshChannel);
    m_sshChannel = nullptr;
    
    return true;
}

wxThread::ExitCode SSHFileThread::Entry() {
    // Initialize SSH log file
    SSHManager::init_log_file();
    
    SSH_LOG("SSHFileThread::Entry - Starting thread");
    
    // Connect to SSH
    if (!ConnectSSH()) {
        SSH_LOG("SSHFileThread::Entry - Failed to connect to SSH");
        wxThreadEvent event(wxEVT_SSH_FILE_LIST);
        event.SetString("ERROR: Failed to connect to SSH");
        wxQueueEvent(m_handler, event.Clone());
        return (ExitCode)1;
    }
    
    SSH_LOG("SSHFileThread::Entry - Connected to SSH successfully");
    
    while (!m_shouldStop) {
        DirectoryRequest req;
        {
            wxMutexLocker lock(m_mutex);
            if (m_requestQueue.empty()) {
                SSH_LOG("SSHFileThread::Entry - Waiting for requests");
                m_condition.Wait();
                if (m_shouldStop) break;
                continue;
            }
            req = m_requestQueue.front();
            m_requestQueue.pop();
        }
        
        if (req.isCommand) {
            SSH_LOG("SSHFileThread::Entry - Processing command: " << req.path);
            std::string output;
            if (ExecuteCommand(req.path.ToStdString(), output)) {
                SSH_LOG("SSHFileThread::Entry - Command executed successfully");
                wxCommandEvent event(wxEVT_SSH_COMMAND_OUTPUT);
                event.SetString(wxString(output.c_str(), wxConvUTF8));
                wxQueueEvent(m_handler, event.Clone());
            }
        } else {
            SSH_LOG("SSHFileThread::Entry - Processing request for path: " << req.path);
            
            // Execute ls command to get directory listing
            wxString command = "ls -la " + req.path;
            std::string output;
            
            if (ExecuteCommand(command.ToStdString(), output)) {
                SSH_LOG("SSHFileThread::Entry - Command executed successfully, output length: " << output.length());
                // Prepend the path to the output so the dialog knows which directory this is
                wxString fullOutput = req.path + "\n" + wxString(output.c_str(), wxConvUTF8);
                wxCommandEvent event(wxEVT_SSH_FILE_LIST);
                event.SetString(fullOutput);
                wxQueueEvent(m_handler, event.Clone());
                SSH_LOG("SSHFileThread::Entry - Event queued");
            } else {
                SSH_LOG("SSHFileThread::Entry - Command execution failed");
                wxCommandEvent event(wxEVT_SSH_FILE_LIST);
                event.SetString("ERROR: Failed to list directory");
                wxQueueEvent(m_handler, event.Clone());
            }
        }
    }
    
    SSH_LOG("SSHFileThread::Entry - Thread stopping");
    DisconnectSSH();
    return (ExitCode)0;
}
