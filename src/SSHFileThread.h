#ifndef SSHFILETHREAD_H
#define SSHFILETHREAD_H

#include <wx/wx.h>
#include <wx/thread.h>
#include <wx/treectrl.h>
#include <queue>
#include <map>
#include <libssh2.h>
#include "DeviceConfig.h"

struct FileItem {
    wxString name;
    bool isDirectory;
    wxString path;
};

wxDECLARE_EVENT(wxEVT_SSH_FILE_LIST, wxCommandEvent);
wxDECLARE_EVENT(wxEVT_SSH_COMMAND_OUTPUT, wxCommandEvent);

class SSHFileThread : public wxThread {
public:
    SSHFileThread(wxEvtHandler* handler, const DeviceConfig& deviceConfig);
    virtual ~SSHFileThread();

    void RequestDirectory(const wxString& path);
    void ExecuteCommand(const wxString& command);
    void Stop();

protected:
    virtual ExitCode Entry() override;

private:
    bool ConnectSSH();
    void DisconnectSSH();
    bool ExecuteCommand(const std::string& command, std::string& output);

    wxEvtHandler* m_handler;
    DeviceConfig m_deviceConfig;
    LIBSSH2_SESSION* m_sshSession;
    LIBSSH2_CHANNEL* m_sshChannel;
    bool m_shouldStop;
    
    struct DirectoryRequest {
        wxString path;
        bool isCommand;
    };
    std::queue<DirectoryRequest> m_requestQueue;
    wxMutex m_mutex;
    wxCondition m_condition;
};

#endif
