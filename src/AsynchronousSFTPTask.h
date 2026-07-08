#ifndef ASYNCHRONOUSSFTPTASK_H
#define ASYNCHRONOUSSFTPTASK_H

#include <wx/wx.h>
#include <wx/socket.h>
#include <wx/timer.h>
#include <queue>
#include <vector>
#include <string>
#include <libssh2.h>
#include <libssh2_sftp.h>
#include "DeviceConfig.h"
#include "FileTransferTask.h"

wxDECLARE_EVENT(wxEVT_FILE_TRANSFER_PROGRESS, wxCommandEvent);
wxDECLARE_EVENT(wxEVT_FILE_TRANSFER_COMPLETE, wxCommandEvent);
wxDECLARE_EVENT(wxEVT_SSH_FILE_LIST, wxCommandEvent);
wxDECLARE_EVENT(wxEVT_SSH_COMMAND_OUTPUT, wxCommandEvent);

class AsynchronousSFTPTask : public wxEvtHandler {
public:
    // Create a list directory task (bypasses 3-active limit)
    static AsynchronousSFTPTask* CreateListTask(wxEvtHandler* handler, const DeviceConfig& deviceConfig, const wxString& path);

    // Create a command execution task (bypasses 3-active limit)
    static AsynchronousSFTPTask* CreateCommandTask(wxEvtHandler* handler, const DeviceConfig& deviceConfig, const wxString& command);

    // Create a file transfer task (subject to 3-active limit)
    static void QueueTransferTask(wxEvtHandler* handler, const DeviceConfig& deviceConfig, const FileTransferTask& task);

    virtual ~AsynchronousSFTPTask();

    void Start();
    void Cancel();

    bool IsFinished() const;
    bool IsRunning() const;

    std::string GetId() const { return m_task.id; }
    FileTransferTask GetTask() const { return m_task; }

    static void CleanAllActiveTasks();

private:
    AsynchronousSFTPTask(wxEvtHandler* handler, const DeviceConfig& deviceConfig);

    void OnTimer(wxTimerEvent& event);
    void DoStateMachineStep();
    void Cleanup();
    void Fail(const std::string& errorMsg);
    void Succeed();

    static void ManageQueue();

private:
    enum class SFTPState {
        CONNECTING,
        HANDSHAKE,
        AUTHENTICATING,
        SFTP_INIT,
        
        // Command / List Specific
        OPEN_CHANNEL,
        EXEC_CMD,
        READ_CMD,
        CLOSE_CHANNEL,

        // File Transfer Specific
        OPEN_FILE,
        TRANSFERRING,
        CLOSE_FILE,
        
        CLEANUP,
        FINISHED,
        FAILED
    };

    wxEvtHandler* m_handler;
    DeviceConfig m_deviceConfig;
    FileTransferTask m_task;
    wxString m_remotePath;
    wxString m_command;
    bool m_isList;
    bool m_isCommand;

    SFTPState m_state;
    wxSocketClient* m_socket;
    LIBSSH2_SESSION* m_sshSession;
    LIBSSH2_SFTP* m_sftpSession;
    LIBSSH2_SFTP_HANDLE* m_sftpHandle;
    LIBSSH2_CHANNEL* m_sshChannel;
    FILE* m_localFile;
    long long m_bytesTransferred;
    std::string m_cmdOutput;
    std::vector<char> m_cacheBuffer; // For uploading partial block management
    
    wxTimer m_timer;
    static const int TIMER_ID = 30001;

    // Queue limits
    static std::vector<AsynchronousSFTPTask*> s_activeTasks;
    static std::queue<AsynchronousSFTPTask*> s_pendingTasks;
    static const int MAX_ACTIVE_TRANSFERS = 3;
};

#endif // ASYNCHRONOUSSFTPTASK_H
