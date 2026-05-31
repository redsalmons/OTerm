#ifndef FILETRANSFERDIALOG_H
#define FILETRANSFERDIALOG_H

#include <wx/wx.h>
#include <wx/listctrl.h>
#include <wx/imaglist.h>
#include <wx/dnd.h>
#include <wx/datetime.h>
#include <wx/filename.h>
#include <wx/strconv.h>
#include <map>
#include <nlohmann/json.hpp>
#include "DeviceConfig.h"

class SSHFileThread;

wxDECLARE_EVENT(wxEVT_FILE_TRANSFER_REQUEST, wxCommandEvent);

class FileDropTarget : public wxDropTarget {
public:
    FileDropTarget(wxEvtHandler* handler, const wxString& deviceID, const wxString& remotePath, bool isDownload = false)
        : wxDropTarget(isDownload ? (wxDataObject*)new wxTextDataObject() : (wxDataObject*)new wxFileDataObject()), 
          m_handler(handler), m_deviceID(deviceID), m_remotePath(remotePath), m_isDownload(isDownload) {}
    
    virtual wxDragResult OnDragOver(wxCoord x, wxCoord y, wxDragResult def) override {
        return wxDragCopy;
    }
    
    virtual wxDragResult OnData(wxCoord x, wxCoord y, wxDragResult def) override {
        if (!GetData()) return wxDragNone;
        
        wxString localPath;
        wxString remotePath;
        long long fileSize = 0;
        
        if (m_isDownload) {
            // Download: remote file dragged to local
            wxTextDataObject* data = (wxTextDataObject*)GetDataObject();
            wxString dragData = data->GetText();
            
            // Parse drag data (format: "path|size")
            size_t pipePos = dragData.find('|');
            if (pipePos != wxString::npos && pipePos + 1 < dragData.length()) {
                remotePath = dragData.substr(0, pipePos);
                wxString sizeStr = dragData.substr(pipePos + 1);
                long long tempSize = 0;
                sizeStr.ToLongLong(&tempSize);
                fileSize = tempSize;
            } else {
                remotePath = dragData;
                fileSize = 0;
            }
            
            // Extract filename from remote path and append to local path
            size_t lastSlash = remotePath.rfind('/');
            if (lastSlash != wxString::npos && lastSlash + 1 < remotePath.length()) {
                wxString fileName = remotePath.substr(lastSlash + 1);
                localPath = m_remotePath;  // m_remotePath stores local target path for download
                if (!localPath.EndsWith(wxFileName::GetPathSeparator())) {
                    localPath += wxFileName::GetPathSeparator();
                }
                localPath += fileName;
            } else {
                localPath = m_remotePath;
            }
        } else {
            // Upload: local file dragged to remote
            wxFileDataObject* data = (wxFileDataObject*)GetDataObject();
            const wxArrayString& files = data->GetFilenames();
            if (files.IsEmpty()) return wxDragNone;
            localPath = files[0];
            
            // Extract filename from local path and append to remote path
            wxFileName fn(localPath);
            wxString fileName = fn.GetFullName();
            remotePath = m_remotePath;
            if (!remotePath.EndsWith("/")) {
                remotePath += "/";
            }
            remotePath += fileName;
            
            // Get file size
            if (fn.FileExists()) {
                wxULongLong size = fn.GetSize();
                fileSize = size.GetValue();
            }
        }
        
        // Generate UUID
        wxString uuid = GenerateUUID();
        
        // Get current date/time
        wxDateTime now = wxDateTime::Now();
        wxString createDate = now.FormatISOCombined();
        
        // Action depends on direction
        wxString action = m_isDownload ? "download" : "upload";
        
        // Create JSON using nlohmann::json to handle escaping properly
        nlohmann::json j;
        j["id"] = uuid.ToStdString();
        j["device_id"] = m_deviceID.ToStdString();
        j["create_date"] = createDate.ToStdString();
        j["local"] = localPath.ToStdString();
        j["remote"] = remotePath.ToStdString();
        j["action"] = action.ToStdString();
        j["size"] = fileSize;
        
        std::string jsonStr = j.dump();
        wxString json(jsonStr.c_str(), wxConvUTF8);
        
        // Send event to handler with transfer info
        wxCommandEvent event(wxEVT_FILE_TRANSFER_REQUEST);
        event.SetString(json);
        wxQueueEvent(m_handler, event.Clone());
        
        return wxDragCopy;
    }
    
    void UpdateRemotePath(const wxString& path) {
        m_remotePath = path;
    }
    
    void UpdateLocalPath(const wxString& path) {
        m_remotePath = path;  // For download, m_remotePath stores the local target path
    }
    
    void SetIsDownload(bool isDownload) {
        m_isDownload = isDownload;
    }
    
private:
    wxString GenerateUUID() {
        // Simple UUID v4 generator
        wxString uuid;
        for (int i = 0; i < 32; i++) {
            if (i == 8 || i == 12 || i == 16 || i == 20) {
                uuid += "-";
            }
            int digit = rand() % 16;
            if (digit < 10) {
                uuid += wxString::Format("%d", digit);
            } else {
                uuid += wxString::Format("%c", 'a' + (digit - 10));
            }
        }
        return uuid;
    }
    
    wxEvtHandler* m_handler;
    wxString m_deviceID;
    wxString m_remotePath;
    bool m_isDownload;
};

class FileTransferDialog : public wxDialog {
public:
    FileTransferDialog(wxWindow* parent, const wxString& title, const DeviceConfig& deviceConfig);
    virtual ~FileTransferDialog();

    void OnSSHFileList(wxCommandEvent& event);
    void OnSSHCommandOutput(wxCommandEvent& event);
    void OnTimer(wxTimerEvent& event);
    void OnSize(wxSizeEvent& event);

private:
    void CreateControls();
    void LayoutControls();
    void PopulateLocalList(const wxString& path);
    void PopulateRemoteList();
    void RequestRemoteDirectory(const wxString& path);
    void CreateImageList();
    wxString FormatFileSize(long long bytes);
    wxString FormatFileDate(const wxDateTime& date);
    void LoadAndDisplayTasks();
    void AdjustTaskListColumns();
    
    struct FileItem {
        wxString name;
        wxString size;
        wxString date;
        wxString permissions;
        bool isDirectory;
        long long sizeBytes;  // Store actual size in bytes
    };

    wxListCtrl* m_localList;
    wxListCtrl* m_remoteList;
    wxListCtrl* m_taskList;
    wxStaticText* m_localPathLabel;
    wxStaticText* m_remotePathLabel;
    FileDropTarget* m_remoteDropTarget;
    FileDropTarget* m_localDropTarget;
    wxImageList* m_imageList;
    DeviceConfig m_deviceConfig;
    SSHFileThread* m_sshThread;
    wxString m_localCurrentPath;
    wxString m_remoteCurrentPath;
    std::map<wxString, bool> m_expandingItems;
    wxTimer* m_taskTimer;
    
    enum {
        ICON_FOLDER = 0,
        ICON_FILE = 1,
        ICON_LOADING = 2
    };
};

#endif
