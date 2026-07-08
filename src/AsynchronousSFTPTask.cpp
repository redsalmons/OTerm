#include "AsynchronousSFTPTask.h"
#include "GlobalConfig.h"
#include "TranslationHelper.h"
#include <wx/app.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <iostream>

wxDEFINE_EVENT(wxEVT_FILE_TRANSFER_PROGRESS, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_FILE_TRANSFER_COMPLETE, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_SSH_FILE_LIST, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_SSH_COMMAND_OUTPUT, wxCommandEvent);

// Initialize static members
std::vector<AsynchronousSFTPTask*> AsynchronousSFTPTask::s_activeTasks;
std::queue<AsynchronousSFTPTask*> AsynchronousSFTPTask::s_pendingTasks;

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
      m_timer(this, TIMER_ID) {
    
    Bind(wxEVT_TIMER, &AsynchronousSFTPTask::OnTimer, this, TIMER_ID);
}

AsynchronousSFTPTask::~AsynchronousSFTPTask() {
    m_timer.Stop();
    Cleanup();
}

AsynchronousSFTPTask* AsynchronousSFTPTask::CreateListTask(wxEvtHandler* handler, const DeviceConfig& deviceConfig, const wxString& path) {
    AsynchronousSFTPTask* task = new AsynchronousSFTPTask(handler, deviceConfig);
    task->m_remotePath = path;
    task->m_isList = true;
    task->Start();
    return task;
}

AsynchronousSFTPTask* AsynchronousSFTPTask::CreateCommandTask(wxEvtHandler* handler, const DeviceConfig& deviceConfig, const wxString& command) {
    AsynchronousSFTPTask* task = new AsynchronousSFTPTask(handler, deviceConfig);
    task->m_command = command;
    task->m_isCommand = true;
    task->Start();
    return task;
}

void AsynchronousSFTPTask::QueueTransferTask(wxEvtHandler* handler, const DeviceConfig& deviceConfig, const FileTransferTask& taskInfo) {
    AsynchronousSFTPTask* task = new AsynchronousSFTPTask(handler, deviceConfig);
    task->m_task = taskInfo;
    
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
    m_state = SFTPState::CONNECTING;
    m_socket = new wxSocketClient(wxSOCKET_NOWAIT);
    
    wxIPaddress* addr;
    wxIPV4address addr4;
    addr = &addr4;
    
    long portLong = 22;
    if (!m_deviceConfig.port.empty()) {
        try {
            portLong = std::stol(m_deviceConfig.port);
        } catch (...) {
            portLong = 22;
        }
    }
    
    addr->Hostname(m_deviceConfig.address);
    addr->Service(portLong);
    
    m_socket->Connect(*addr, false); // non-blocking connect
    
    m_sshSession = libssh2_session_init();
    if (!m_sshSession) {
        Fail("Failed to initialize libssh2 session");
        return;
    }
    
    // Start timer for polling loop (15ms tick)
    m_timer.Start(15);
}

void AsynchronousSFTPTask::Cancel() {
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

void AsynchronousSFTPTask::DoStateMachineStep() {
    if (IsFinished()) {
        m_timer.Stop();
        return;
    }

    switch (m_state) {
        case SFTPState::CONNECTING: {
            if (m_socket->IsConnected()) {
                m_state = SFTPState::HANDSHAKE;
            } else if (m_socket->WaitOnConnect(0, 0)) {
                m_state = SFTPState::HANDSHAKE;
            } else if (m_socket->Error()) {
                Fail("Failed to connect to SSH server " + m_deviceConfig.address);
            }
            break;
        }

        case SFTPState::HANDSHAKE: {
            libssh2_session_set_blocking(m_sshSession, 0);
            int sockfd = m_socket->GetSocket();
            int rc = libssh2_session_handshake(m_sshSession, sockfd);
            if (rc == 0) {
                m_state = SFTPState::AUTHENTICATING;
            } else if (rc == LIBSSH2_ERROR_EAGAIN) {
                // Retry next tick
            } else {
                Fail("SSH handshake failed: error " + std::to_string(rc));
            }
            break;
        }

        case SFTPState::AUTHENTICATING: {
            int rc = libssh2_userauth_password(m_sshSession, m_deviceConfig.username.c_str(), m_deviceConfig.password.c_str());
            if (rc == 0) {
                if (m_isList || m_isCommand) {
                    m_state = SFTPState::OPEN_CHANNEL;
                } else {
                    m_state = SFTPState::SFTP_INIT;
                }
            } else if (rc == LIBSSH2_ERROR_EAGAIN) {
                // Retry next tick
            } else {
                Fail("Authentication failed for user " + m_deviceConfig.username);
            }
            break;
        }

        case SFTPState::SFTP_INIT: {
            m_sftpSession = libssh2_sftp_init(m_sshSession);
            if (m_sftpSession) {
                m_state = SFTPState::OPEN_FILE;
            } else {
                int err = libssh2_session_last_errno(m_sshSession);
                if (err == LIBSSH2_ERROR_EAGAIN) {
                    // Retry
                } else {
                    Fail("SFTP initialization failed");
                }
            }
            break;
        }

        case SFTPState::OPEN_CHANNEL: {
            m_sshChannel = libssh2_channel_open_session(m_sshSession);
            if (m_sshChannel) {
                m_state = SFTPState::EXEC_CMD;
            } else {
                int err = libssh2_session_last_errno(m_sshSession);
                if (err == LIBSSH2_ERROR_EAGAIN) {
                    // Retry
                } else {
                    Fail("Failed to open SSH session channel");
                }
            }
            break;
        }

        case SFTPState::EXEC_CMD: {
            std::string cmd = m_isList ? ("ls -la " + m_remotePath.ToStdString()) : m_command.ToStdString();
            int rc = libssh2_channel_exec(m_sshChannel, cmd.c_str());
            if (rc == 0) {
                m_state = SFTPState::READ_CMD;
            } else if (rc == LIBSSH2_ERROR_EAGAIN) {
                // Retry
            } else {
                Fail("Failed to execute command: " + cmd);
            }
            break;
        }

        case SFTPState::READ_CMD: {
            char buffer[4096];
            while (true) {
                int rc = libssh2_channel_read(m_sshChannel, buffer, sizeof(buffer));
                if (rc > 0) {
                    m_cmdOutput.append(buffer, rc);
                } else if (rc == LIBSSH2_ERROR_EAGAIN) {
                    break; // wait for next tick
                } else if (rc == 0) {
                    m_state = SFTPState::CLOSE_CHANNEL;
                    break;
                } else {
                    Fail("Error reading command execution output");
                    break;
                }
            }
            break;
        }

        case SFTPState::CLOSE_CHANNEL: {
            int rc = libssh2_channel_close(m_sshChannel);
            if (rc == 0 || rc != LIBSSH2_ERROR_EAGAIN) {
                libssh2_channel_free(m_sshChannel);
                m_sshChannel = nullptr;
                m_state = SFTPState::CLEANUP;
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
                    m_localFile = fopen(m_task.local.c_str(), "rb");
                    if (!m_localFile) {
                        Fail("Failed to open local file for reading: " + m_task.local);
                    } else {
                        m_state = SFTPState::TRANSFERRING;
                    }
                } else {
                    int err = libssh2_session_last_errno(m_sshSession);
                    if (err == LIBSSH2_ERROR_EAGAIN) {
                        // Retry next tick
                    } else {
                        Fail("Failed to open remote file for writing: " + m_task.remote);
                    }
                }
            } else { // download
                m_sftpHandle = libssh2_sftp_open_ex(
                    m_sftpSession, m_task.remote.c_str(), m_task.remote.length(),
                    LIBSSH2_FXF_READ, 0, LIBSSH2_SFTP_OPENFILE
                );
                if (m_sftpHandle) {
                    m_localFile = fopen(m_task.local.c_str(), "wb");
                    if (!m_localFile) {
                        Fail("Failed to open local file for writing: " + m_task.local);
                    } else {
                        m_state = SFTPState::TRANSFERRING;
                    }
                } else {
                    int err = libssh2_session_last_errno(m_sshSession);
                    if (err == LIBSSH2_ERROR_EAGAIN) {
                        // Retry next tick
                    } else {
                        Fail("Failed to open remote file for reading: " + m_task.remote);
                    }
                }
            }
            break;
        }

        case SFTPState::TRANSFERRING: {
            char buffer[32768];
            bool blocked = false;
            while (!blocked) {
                if (m_task.action == "upload") {
                    if (m_cacheBuffer.empty()) {
                        size_t bytesRead = fread(buffer, 1, sizeof(buffer), m_localFile);
                        if (bytesRead > 0) {
                            m_cacheBuffer.assign(buffer, buffer + bytesRead);
                        } else {
                            // EOF local file
                            m_state = SFTPState::CLOSE_FILE;
                            break;
                        }
                    }
                    
                    int rc = libssh2_sftp_write(m_sftpHandle, m_cacheBuffer.data(), m_cacheBuffer.size());
                    if (rc > 0) {
                        m_bytesTransferred += rc;
                        m_cacheBuffer.erase(m_cacheBuffer.begin(), m_cacheBuffer.begin() + rc);
                        
                        // Send progress event
                        if (m_task.size > 0) {
                            int progress = static_cast<int>((m_bytesTransferred * 100) / m_task.size);
                            if (progress > 100) progress = 100;
                            wxCommandEvent progressEvt(wxEVT_FILE_TRANSFER_PROGRESS);
                            nlohmann::json pj;
                            pj["id"] = m_task.id;
                            pj["progress"] = progress;
                            pj["status"] = "processing";
                            progressEvt.SetString(pj.dump().c_str());
                            wxQueueEvent(m_handler, progressEvt.Clone());
                        }
                    } else if (rc == LIBSSH2_ERROR_EAGAIN) {
                        blocked = true;
                    } else {
                        Fail("SFTP write failed");
                        blocked = true;
                    }
                } else { // download
                    int rc = libssh2_sftp_read(m_sftpHandle, buffer, sizeof(buffer));
                    if (rc > 0) {
                        size_t written = fwrite(buffer, 1, rc, m_localFile);
                        if (written < (size_t)rc) {
                            Fail("Local file write failed (disk full?)");
                            blocked = true;
                            break;
                        }
                        m_bytesTransferred += rc;
                        
                        // Send progress event
                        if (m_task.size > 0) {
                            int progress = static_cast<int>((m_bytesTransferred * 100) / m_task.size);
                            if (progress > 100) progress = 100;
                            wxCommandEvent progressEvt(wxEVT_FILE_TRANSFER_PROGRESS);
                            nlohmann::json pj;
                            pj["id"] = m_task.id;
                            pj["progress"] = progress;
                            pj["status"] = "processing";
                            progressEvt.SetString(pj.dump().c_str());
                            wxQueueEvent(m_handler, progressEvt.Clone());
                        }
                    } else if (rc == 0) {
                        // EOF remote file
                        m_state = SFTPState::CLOSE_FILE;
                        break;
                    } else if (rc == LIBSSH2_ERROR_EAGAIN) {
                        blocked = true;
                    } else {
                        Fail("SFTP read failed");
                        blocked = true;
                    }
                }
            }
            break;
        }

        case SFTPState::CLOSE_FILE: {
            if (m_localFile) {
                fclose(m_localFile);
                m_localFile = nullptr;
            }
            int rc = libssh2_sftp_close(m_sftpHandle);
            if (rc == 0 || rc != LIBSSH2_ERROR_EAGAIN) {
                m_sftpHandle = nullptr;
                m_state = SFTPState::CLEANUP;
            }
            break;
        }

        case SFTPState::CLEANUP: {
            if (m_sftpSession) {
                int rc = libssh2_sftp_shutdown(m_sftpSession);
                if (rc == LIBSSH2_ERROR_EAGAIN) return;
                m_sftpSession = nullptr;
            }
            if (m_sshSession) {
                int rc = libssh2_session_disconnect(m_sshSession, "Normal shutdown");
                if (rc == LIBSSH2_ERROR_EAGAIN) return;
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
    m_state = SFTPState::FAILED;
    m_timer.Stop();
    
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
    }
}

void AsynchronousSFTPTask::Succeed() {
    m_state = SFTPState::FINISHED;
    m_timer.Stop();
    
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
    }
}

void AsynchronousSFTPTask::Cleanup() {
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
