#include "FileTransferThread.h"
#include "GlobalConfig.h"
#include "TranslationHelper.h"
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <fstream>
#include <iostream>
#include <cstring>
#include <ctime>
#include <cerrno>
#include "SSHManager.h"

wxDEFINE_EVENT(wxEVT_FILE_TRANSFER_PROGRESS, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_FILE_TRANSFER_COMPLETE, wxCommandEvent);

FileTransferThread::FileTransferThread(wxEvtHandler* handler, const DeviceConfig& deviceConfig)
    : wxThread(wxTHREAD_DETACHED), m_handler(handler), m_deviceConfig(deviceConfig),
      m_sshSession(nullptr), m_sftpSession(nullptr), m_shouldStop(false), m_connected(false), m_condition(m_mutex) {
    uv_loop_init(&m_loop);
    // Don't load task list from file - only process new tasks
}

FileTransferThread::~FileTransferThread() {
    Stop();
}

void FileTransferThread::AddTask(const FileTransferTask& task) {
    wxMutexLocker lock(m_mutex);
    m_taskQueue.push(task);
    m_taskList.push_back(task);
    SaveTaskList();
    m_condition.Signal();
}

void FileTransferThread::Stop() {
    {
        wxMutexLocker lock(m_mutex);
        m_shouldStop = true;
    }
    m_condition.Signal();
    
    if (IsRunning()) {
        Wait();
    }
    
    DisconnectSSH();
}

void FileTransferThread::OnConnect(uv_connect_t* req, int status) {
    FileTransferThread* thread = static_cast<FileTransferThread*>(req->data);
    if (status == 0) {
        thread->m_connected = true;
        SSH_LOG("SSH connection established");
    } else {
        SSH_LOG("Connection failed: " << uv_strerror(status));
    }
}

bool FileTransferThread::ConnectSSH() {
    SSH_LOG("Starting SSH connection to " << m_deviceConfig.address << ":" << m_deviceConfig.port);
    
    // Initialize libuv TCP handle
    uv_tcp_init(&m_loop, &m_tcpHandle);

    // Parse address
    if (m_deviceConfig.port.empty()) {
        SSH_LOG("FileTransferThread: Port is empty, cannot connect");
        return false;
    }
    struct sockaddr_in addr;
    uv_ip4_addr(m_deviceConfig.address.c_str(), std::stoi(m_deviceConfig.port), &addr);
    
    // Setup connect request
    m_connectReq.data = this;
    
    // Connect using libuv
    int rc = uv_tcp_connect(&m_connectReq, &m_tcpHandle, (const struct sockaddr*)&addr, OnConnect);
    if (rc != 0) {
        SSH_LOG("Failed to initiate TCP connect: " << uv_strerror(rc));
        return false;
    }
    
    // Run the loop to complete connection
    while (!m_connected && !m_shouldStop) {
        uv_run(&m_loop, UV_RUN_ONCE);
        wxThread::Sleep(10);
    }
    
    if (!m_connected) {
        SSH_LOG("Connection failed - not connected");
        return false;
    }
    
    // Get the socket from libuv
    uv_os_fd_t sock = 0;
    uv_fileno((uv_handle_t*)&m_tcpHandle, &sock);
    
    // Initialize libssh2 session
    m_sshSession = libssh2_session_init();
    if (!m_sshSession) {
        SSH_LOG("Failed to initialize libssh2 session");
        return false;
    }
    
    // Set non-blocking mode
    libssh2_session_set_blocking(m_sshSession, 0);
    
    // Perform handshake
#ifdef _WIN32
    rc = libssh2_session_handshake(m_sshSession, static_cast<int>(reinterpret_cast<uintptr_t>(sock)));
#else
    rc = libssh2_session_handshake(m_sshSession, static_cast<int>(sock));
#endif
    while (rc == LIBSSH2_ERROR_EAGAIN) {
        uv_run(&m_loop, UV_RUN_ONCE);
#ifdef _WIN32
        rc = libssh2_session_handshake(m_sshSession, static_cast<int>(reinterpret_cast<uintptr_t>(sock)));
#else
        rc = libssh2_session_handshake(m_sshSession, static_cast<int>(sock));
#endif
    }
    
    if (rc != 0) {
        SSH_LOG("SSH handshake failed");
        libssh2_session_free(m_sshSession);
        m_sshSession = nullptr;
        return false;
    }
    
    SSH_LOG("SSH handshake successful");
    
    // Set blocking mode for authentication
    libssh2_session_set_blocking(m_sshSession, 1);
    
    // Authenticate
    if (libssh2_userauth_password(m_sshSession, m_deviceConfig.username.c_str(), 
                                  m_deviceConfig.password.c_str()) != 0) {
        SSH_LOG("Authentication failed");
        libssh2_session_disconnect(m_sshSession, "Authentication failed");
        libssh2_session_free(m_sshSession);
        m_sshSession = nullptr;
        return false;
    }
    
    SSH_LOG("Authentication successful");
    
    // Initialize SFTP
    m_sftpSession = libssh2_sftp_init(m_sshSession);
    if (!m_sftpSession) {
        SSH_LOG("Failed to initialize SFTP");
        libssh2_session_disconnect(m_sshSession, "SFTP init failed");
        libssh2_session_free(m_sshSession);
        m_sshSession = nullptr;
        return false;
    }
    
    SSH_LOG("SFTP initialized successfully");
    return true;
}

void FileTransferThread::DisconnectSSH() {
    if (m_sftpSession) {
        libssh2_sftp_shutdown(m_sftpSession);
        m_sftpSession = nullptr;
    }
    if (m_sshSession) {
        libssh2_session_disconnect(m_sshSession, "Normal shutdown");
        libssh2_session_free(m_sshSession);
        m_sshSession = nullptr;
    }
    uv_close((uv_handle_t*)&m_tcpHandle, nullptr);
    uv_loop_close(&m_loop);
}

bool FileTransferThread::UploadFile(const std::string& taskId, const std::string& localPath, const std::string& remotePath, long long fileSize) {
    SSH_LOG("UploadFile called: " << localPath << " -> " << remotePath);
    
    // Open local file
    FILE* localFile = fopen(localPath.c_str(), "rb");
    if (!localFile) {
        SSH_LOG("Failed to open local file: " << localPath << ", error: " << strerror(errno));
        UpdateTaskProgress(taskId, 0, "failed", "无法打开本地文件: " + localPath);
        return false;
    }
    SSH_LOG("Local file opened successfully");

    // Create remote file via SFTP
    LIBSSH2_SFTP_HANDLE* sftpHandle = libssh2_sftp_open(m_sftpSession, remotePath.c_str(),
                                                        LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC,
                                                        0644);
    if (!sftpHandle) {
        int sftpError = libssh2_sftp_last_error(m_sftpSession);
        SSH_LOG("Failed to open remote file: " << remotePath << ", SFTP error code: " << sftpError);
        fclose(localFile);
        UpdateTaskProgress(taskId, 0, "failed", "无法打开远程文件: " + remotePath + " (错误码: " + std::to_string(sftpError) + ")");
        return false;
    }
    SSH_LOG("Remote file opened successfully");

    // Transfer data in chunks
    const int BUFFER_SIZE = 16384;
    char buffer[BUFFER_SIZE];
    long long totalTransferred = 0;
    int lastProgress = 0;
    time_t lastUpdateTime = time(nullptr);

    while (!feof(localFile) && !m_shouldStop) {
        size_t bytesRead = fread(buffer, 1, BUFFER_SIZE, localFile);
        if (bytesRead > 0) {
            ssize_t bytesWritten = libssh2_sftp_write(sftpHandle, buffer, bytesRead);
            if (bytesWritten < 0) {
                int sftpError = libssh2_sftp_last_error(m_sftpSession);
                SSH_LOG("Failed to write to remote file, SFTP error code: " << sftpError << ", bytes read: " << bytesRead);
                libssh2_sftp_close(sftpHandle);
                fclose(localFile);
                UpdateTaskProgress(taskId, 0, "failed", "写入远程文件失败 (错误码: " + std::to_string(sftpError) + ")");
                return false;
            }

            totalTransferred += bytesWritten;

            // Update progress every second
            time_t currentTime = time(nullptr);
            if (currentTime - lastUpdateTime >= 1) {
                int progress = (int)((totalTransferred * 100) / fileSize);
                if (progress != lastProgress) {
                    SSH_LOG("Upload progress: " << progress << "% (" << totalTransferred << "/" << fileSize << " bytes)");
                    UpdateTaskProgress(taskId, progress, "transferring");
                    SendProgressEvent(taskId, progress, "processing");
                    lastProgress = progress;
                }
                lastUpdateTime = currentTime;
            }
        }
    }

    libssh2_sftp_close(sftpHandle);
    fclose(localFile);
    SSH_LOG("Upload completed, transferred: " << totalTransferred << " bytes");

    if (m_shouldStop) {
        return false;
    }

    return true;
}

bool FileTransferThread::DownloadFile(const std::string& taskId, const std::string& remotePath, const std::string& localPath, long long fileSize) {
    SSH_LOG("DownloadFile called: " << remotePath << " -> " << localPath);
    
    // Open remote file via SFTP
    LIBSSH2_SFTP_HANDLE* sftpHandle = libssh2_sftp_open(m_sftpSession, remotePath.c_str(),
                                                        LIBSSH2_FXF_READ, 0);
    if (!sftpHandle) {
        int sftpError = libssh2_sftp_last_error(m_sftpSession);
        SSH_LOG("Failed to open remote file: " << remotePath << ", SFTP error code: " << sftpError);
        UpdateTaskProgress(taskId, 0, "failed", "无法打开远程文件: " + remotePath + " (错误码: " + std::to_string(sftpError) + ")");
        return false;
    }
    SSH_LOG("Remote file opened successfully");

    // Create local file
    FILE* localFile = fopen(localPath.c_str(), "wb");
    if (!localFile) {
        SSH_LOG("Failed to create local file: " << localPath << ", error: " << strerror(errno));
        libssh2_sftp_close(sftpHandle);
        UpdateTaskProgress(taskId, 0, "failed", "无法创建本地文件: " + localPath);
        return false;
    }
    SSH_LOG("Local file created successfully");

    // Transfer data in chunks
    const int BUFFER_SIZE = 16384;
    char buffer[BUFFER_SIZE];
    long long totalTransferred = 0;
    int lastProgress = 0;
    time_t lastUpdateTime = time(nullptr);

    while (!m_shouldStop) {
        ssize_t bytesRead = libssh2_sftp_read(sftpHandle, buffer, BUFFER_SIZE);
        if (bytesRead < 0) {
            int sftpError = libssh2_sftp_last_error(m_sftpSession);
            SSH_LOG("Failed to read from remote file, SFTP error code: " << sftpError);
            libssh2_sftp_close(sftpHandle);
            fclose(localFile);
            UpdateTaskProgress(taskId, 0, "failed", "读取远程文件失败 (错误码: " + std::to_string(sftpError) + ")");
            return false;
        }

        if (bytesRead == 0) {
            break;  // End of file
        }

        size_t bytesWritten = fwrite(buffer, 1, bytesRead, localFile);
        if (bytesWritten != (size_t)bytesRead) {
            SSH_LOG("Failed to write to local file, error: " << strerror(errno) << ", bytes read: " << bytesRead << ", bytes written: " << bytesWritten);
            libssh2_sftp_close(sftpHandle);
            fclose(localFile);
            UpdateTaskProgress(taskId, 0, "failed", "写入本地文件失败");
            return false;
        }

        totalTransferred += bytesRead;

        // Update progress every second
        time_t currentTime = time(nullptr);
        if (currentTime - lastUpdateTime >= 1) {
            int progress = (int)((totalTransferred * 100) / fileSize);
            if (progress != lastProgress) {
                SSH_LOG("Download progress: " << progress << "% (" << totalTransferred << "/" << fileSize << " bytes)");
                UpdateTaskProgress(taskId, progress, "transferring");
                SendProgressEvent(taskId, progress, "processing");
                lastProgress = progress;
            }
            lastUpdateTime = currentTime;
        }
    }

    libssh2_sftp_close(sftpHandle);
    fclose(localFile);
    SSH_LOG("Download completed, transferred: " << totalTransferred << " bytes");

    if (m_shouldStop) {
        return false;
    }

    return true;
}

void FileTransferThread::SaveTaskList() {
    wxMutexLocker lock(m_mutex);
    
    // Get workspace directory
    wxString workspaceDir = wxString::FromUTF8(GlobalConfig::GetWorkspacePath().c_str());
    SSH_LOG("Workspace directory: " << workspaceDir.ToStdString());
    
    if (!wxDirExists(workspaceDir)) {
        SSH_LOG("Creating workspace directory: " << workspaceDir.ToStdString());
        wxMkdir(workspaceDir);
    }
    
    wxString filePath = workspaceDir + wxFileName::GetPathSeparator() + 
                       wxString::FromUTF8(m_deviceConfig.id.c_str()) + "task.json";
    SSH_LOG("Task file path: " << filePath.ToStdString());
    
    FileTransferTaskList list;
    list.device_id = m_deviceConfig.id;
    list.tasks = m_taskList;
    
    try {
        json j = list.to_json();
        std::ofstream file(filePath.ToStdString());
        file << j.dump(4);
        file.close();
        SSH_LOG("Task list saved successfully");
    } catch (const std::exception& e) {
        SSH_LOG("Failed to save task list: " << e.what());
    }
}

void FileTransferThread::LoadTaskList() {
    wxMutexLocker lock(m_mutex);
    
    wxString workspaceDir = wxString::FromUTF8(GlobalConfig::GetWorkspacePath().c_str());
    wxString filePath = workspaceDir + wxFileName::GetPathSeparator() + 
                       wxString::FromUTF8(m_deviceConfig.id.c_str()) + "task.json";
    
    if (!wxFileExists(filePath)) {
        return;
    }
    
    try {
        std::ifstream file(filePath.ToStdString());
        json j;
        file >> j;
        file.close();
        
        FileTransferTaskList list = FileTransferTaskList::fromJson(j);
        m_taskList = list.tasks;
        
        // Re-queue pending tasks
        for (const auto& task : m_taskList) {
            if (task.status == "pending" || task.status == "waiting") {
                m_taskQueue.push(task);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Failed to load task list: " << e.what() << std::endl;
    }
}

void FileTransferThread::UpdateTaskProgress(const std::string& taskId, int progress, const std::string& status, const std::string& result) {
    wxMutexLocker lock(m_mutex);
    
    for (auto& task : m_taskList) {
        if (task.id == taskId) {
            if (progress >= 0) {
                task.progress = progress;
            }
            if (!status.empty()) {
                task.status = status;
            }
            if (!result.empty()) {
                task.result = result;
            }
            break;
        }
    }
    
    SaveTaskList();
}

void FileTransferThread::ProcessNextTask() {
    FileTransferTask task;
    {
        wxMutexLocker lock(m_mutex);
        if (m_taskQueue.empty()) {
            return;
        }
        task = m_taskQueue.front();
        m_taskQueue.pop();
    }
    
    SSH_LOG("Processing task: " << task.id << ", action: " << task.action);
    SSH_LOG("Local: " << task.local << ", Remote: " << task.remote << ", Size: " << task.size);
    
    // Connect SSH if not connected
    if (!m_sshSession) {
        SSH_LOG("SSH session not connected, connecting...");
        if (!ConnectSSH()) {
            SSH_LOG("SSH connection failed");
            UpdateTaskProgress(task.id, 0, "failed", "SSH连接失败");
            SendProgressEvent(task.id, 0, "failed");
            return;
        }
        SSH_LOG("SSH connection successful");
    }
    
    // Update status to processing
    UpdateTaskProgress(task.id, 0, "transferring", "");
    SendProgressEvent(task.id, 0, "processing");
    
    bool success = false;
    if (task.action == "upload") {
        SSH_LOG("Starting upload...");
        success = UploadFile(task.id, task.local, task.remote, task.size);
        SSH_LOG("Upload completed, success: " << success);
    } else if (task.action == "download") {
        SSH_LOG("Starting download...");
        success = DownloadFile(task.id, task.remote, task.local, task.size);
        SSH_LOG("Download completed, success: " << success);
    }
    
    // Update status to completed
    std::string result = success ? "success" : "transferFailed";
    UpdateTaskProgress(task.id, 100, "completed", result);
    SendProgressEvent(task.id, 100, "completed");
}

void FileTransferThread::SendProgressEvent(const std::string& taskId, int progress, const std::string& status) {
    wxCommandEvent progressEvent(wxEVT_FILE_TRANSFER_PROGRESS);
    progressEvent.SetString(wxString::Format("{\"id\":\"%s\",\"progress\":%d,\"status\":\"%s\"}", 
                                            taskId.c_str(), progress, status.c_str()));
    wxQueueEvent(m_handler, progressEvent.Clone());
}

wxThread::ExitCode FileTransferThread::Entry() {
    while (!m_shouldStop) {
        {
            wxMutexLocker lock(m_mutex);
            if (!m_shouldStop && m_taskQueue.empty()) {
                m_condition.Wait();
            }
        }
        
        if (m_shouldStop) {
            break;
        }
        
        ProcessNextTask();
    }
    
    return (ExitCode)0;
}
