#ifndef FILETRANSFERDIALOG_H
#define FILETRANSFERDIALOG_H

#include <wx/wx.h>
#include <wx/listctrl.h>
#include <wx/dataview.h>
#include <wx/imaglist.h>
#include <wx/dnd.h>
#include <wx/datetime.h>
#include <wx/filename.h>
#include <wx/strconv.h>
#include <map>
#include <vector>
#include <nlohmann/json.hpp>
#include "DeviceConfig.h"

wxDECLARE_EVENT(wxEVT_FILE_TRANSFER_REQUEST, wxCommandEvent);

class FileTransferDialog : public wxDialog {
public:
    FileTransferDialog(wxWindow* parent, const wxString& title, const DeviceConfig& deviceConfig);
    virtual ~FileTransferDialog();

    void OnSSHFileList(wxCommandEvent& event);
    void OnSSHCommandOutput(wxCommandEvent& event);
    void OnTimer(wxTimerEvent& event);
    void OnSize(wxSizeEvent& event);
    void OnFileTransferProgress(wxCommandEvent& event);
    void OnFileTransferComplete(wxCommandEvent& event);
    void SetDeviceConfig(const DeviceConfig& deviceConfig) { m_deviceConfig = deviceConfig; }

private:
    void CreateControls();
    void LayoutControls();
    void PopulateLocalList(const wxString& path);
    void PopulateRemoteList();
    void RequestRemoteDirectory(const wxString& path);
    void CreateIcons();
    wxString FormatFileSize(long long bytes);
    wxString FormatFileDate(const wxDateTime& date);
    void LoadAndDisplayTasks();
    void AdjustTaskListColumns();
    void AdjustFileListColumns();
    
    struct FileItem {
        wxString name;
        wxString size;
        wxString date;
        wxString permissions;
        bool isDirectory;
        long long sizeBytes;  // Store actual size in bytes
    };

    wxDataViewListCtrl* m_localList;
    wxDataViewListCtrl* m_remoteList;
    wxListCtrl* m_taskList;
    wxStaticText* m_localPathLabel;
    wxStaticText* m_remotePathLabel;
    wxIcon m_folderIcon;
    wxIcon m_fileIcon;
    wxIcon m_loadingIcon;
    std::vector<long long> m_remoteFileSizes;
    DeviceConfig m_deviceConfig;
    wxString m_localCurrentPath;
    wxString m_remoteCurrentPath;
    std::map<wxString, bool> m_expandingItems;
    wxTimer* m_taskTimer;
};

#endif
