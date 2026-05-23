#ifndef FILETRANSFERTHREAD_H
#define FILETRANSFERTHREAD_H

#include <wx/wx.h>
#include <wx/thread.h>
#include <queue>
#include <libssh2.h>
#include <libssh2_sftp.h>
#include <uv.h>
#include "DeviceConfig.h"
#include "FileTransferTask.h"

wxDECLARE_EVENT(wxEVT_FILE_TRANSFER_PROGRESS, wxCommandEvent);
wxDECLARE_EVENT(wxEVT_FILE_TRANSFER_COMPLETE, wxCommandEvent);

class FileTransferThread : public wxThread {
public:
    FileTransferThread(wxEvtHandler* handler, const DeviceConfig& deviceConfig);
    virtual ~FileTransferThread();

    void AddTask(const FileTransferTask& task);
    void Stop();

protected:
    virtual ExitCode Entry() override;

private:
    bool ConnectSSH();
    void DisconnectSSH();
    bool UploadFile(const std::string& taskId, const std::string& localPath, const std::string& remotePath, long long fileSize);
    bool DownloadFile(const std::string& taskId, const std::string& remotePath, const std::string& localPath, long long fileSize);
    void SaveTaskList();
    void LoadTaskList();
    void UpdateTaskProgress(const std::string& taskId, int progress, const std::string& status = "", const std::string& result = "");
    void ProcessNextTask();
    void SendProgressEvent(const std::string& taskId, int progress, const std::string& status);
    static void OnConnect(uv_connect_t* req, int status);

    wxEvtHandler* m_handler;
    DeviceConfig m_deviceConfig;
    LIBSSH2_SESSION* m_sshSession;
    LIBSSH2_SFTP* m_sftpSession;
    uv_loop_t m_loop;
    uv_tcp_t m_tcpHandle;
    uv_connect_t m_connectReq;
    bool m_shouldStop;
    bool m_connected;
    
    std::queue<FileTransferTask> m_taskQueue;
    std::vector<FileTransferTask> m_taskList;
    wxMutex m_mutex;
    wxCondition m_condition;
};

#endif
