#include "FileTransferDialog.h"
#include "SSHFileThread.h"
#include "SSHManager.h"
#include "FileTransferTask.h"
#include "FileTransferThread.h"
#include "GlobalConfig.h"
#include "TranslationHelper.h"
#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/regex.h>
#include <vector>
#include <wx/artprov.h>
#include <wx/datetime.h>
#include <wx/dnd.h>
#include <fstream>
#include <sstream>

wxDEFINE_EVENT(wxEVT_FILE_TRANSFER_REQUEST, wxCommandEvent);

FileTransferDialog::FileTransferDialog(wxWindow* parent, const wxString& title, const DeviceConfig& deviceConfig)
    : wxDialog(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      m_deviceConfig(deviceConfig), m_sshThread(nullptr), m_imageList(nullptr),
      m_localList(nullptr), m_remoteList(nullptr), m_taskList(nullptr), m_localPathLabel(nullptr),
      m_remotePathLabel(nullptr), m_remoteDropTarget(nullptr), m_localDropTarget(nullptr),
      m_localCurrentPath(
#ifdef _WIN32
          wxEmptyString
#else
          wxGetHomeDir()
#endif
      ), m_remoteCurrentPath("~"), m_taskTimer(nullptr)
{
    double dpiScale = GlobalConfig::GetDPIScaleFactor();
    int baseWidth = 834;
    int baseHeight = 667;
    SetSize(wxSize(static_cast<int>(baseWidth * dpiScale), static_cast<int>(baseHeight * dpiScale)));

    SSH_LOG("FileTransferDialog constructor - Creating dialog");
    
    Bind(wxEVT_SSH_FILE_LIST, &FileTransferDialog::OnSSHFileList, this);
    Bind(wxEVT_SSH_COMMAND_OUTPUT, &FileTransferDialog::OnSSHCommandOutput, this);
    Bind(wxEVT_FILE_TRANSFER_PROGRESS, &FileTransferDialog::OnFileTransferProgress, this);
    Bind(wxEVT_FILE_TRANSFER_COMPLETE, &FileTransferDialog::OnFileTransferComplete, this);
    
    CreateImageList();
    CreateControls();
    LayoutControls();
    
    // Start SSH thread
    SSH_LOG("FileTransferDialog constructor - Creating SSH thread");
    m_sshThread = new SSHFileThread(this, m_deviceConfig);
    SSH_LOG("FileTransferDialog constructor - Starting SSH thread");
    m_sshThread->Run();
    SSH_LOG("FileTransferDialog constructor - SSH thread started");
    
    // Populate local list
    PopulateLocalList(m_localCurrentPath);
    
    // Get the full path of ~ by executing pwd, then request directory listing
    SSH_LOG("FileTransferDialog constructor - Getting full path with pwd");
    if (m_sshThread) {
        m_sshThread->ExecuteCommand("pwd");
    }
    SSH_LOG("FileTransferDialog constructor - pwd command sent");
    
    // Create and start timer for task list refresh (every 3 seconds)
    m_taskTimer = new wxTimer(this);
    Bind(wxEVT_TIMER, &FileTransferDialog::OnTimer, this, m_taskTimer->GetId());
    m_taskTimer->Start(3000);  // 3000ms = 3 seconds
    
    // Bind size event to adjust task list columns when window is resized
    Bind(wxEVT_SIZE, &FileTransferDialog::OnSize, this);
    
    // Load and display initial tasks
    LoadAndDisplayTasks();
    
    // Make non-modal
    Show();
}

FileTransferDialog::~FileTransferDialog() {
    if (m_taskTimer) {
        m_taskTimer->Stop();
        delete m_taskTimer;
    }
    if (m_sshThread) {
        m_sshThread->Stop();
        delete m_sshThread;
    }
    if (m_imageList) {
        delete m_imageList;
    }
    if (m_remoteDropTarget) {
        delete m_remoteDropTarget;
    }
    if (m_localDropTarget) {
        delete m_localDropTarget;
    }
}

void FileTransferDialog::CreateImageList() {
    // Create image list with folder, file, and loading icons
    m_imageList = new wxImageList(16, 16, true);
    
    // Add folder icon
    wxIcon folderIcon = wxArtProvider::GetIcon(wxART_FOLDER, wxART_OTHER, wxSize(16, 16));
    m_imageList->Add(folderIcon);
    
    // Add file icon
    wxIcon fileIcon = wxArtProvider::GetIcon(wxART_NORMAL_FILE, wxART_OTHER, wxSize(16, 16));
    m_imageList->Add(fileIcon);
    
    // Add loading icon (use a different icon for loading state)
    wxIcon loadingIcon = wxArtProvider::GetIcon(wxART_EXECUTABLE_FILE, wxART_OTHER, wxSize(16, 16));
    m_imageList->Add(loadingIcon);
}

void FileTransferDialog::OnSSHFileList(wxCommandEvent& event) {
    SSH_LOG("OnSSHFileList called - START");
    
    wxString output = event.GetString();
    
    // Extract the path from the event (first line contains the path)
    wxArrayString lines = wxSplit(output, '\n');
    if (lines.IsEmpty()) {
        SSH_LOG("Empty output");
        return;
    }
    
    wxString targetPath = lines[0];
    wxString dirContent = output.Mid(targetPath.length() + 1);
    
    SSH_LOG("Target path: " << targetPath);
    SSH_LOG("Dir content length: " << dirContent.length());
    
    if (dirContent.StartsWith("ERROR:")) {
        SSH_LOG("SSH file list error: " << dirContent);
        return;
    }
    
    if (!m_remoteList) {
        SSH_LOG("m_remoteList is null");
        return;
    }
    
    // Clear existing items
    m_remoteList->DeleteAllItems();
    
    // Add parent directory entry
    long parentIndex = m_remoteList->InsertItem(0, "..", ICON_FOLDER);
    m_remoteList->SetItem(parentIndex, 1, "");
    m_remoteList->SetItem(parentIndex, 2, "");
    
    // Parse ls -la output
    wxArrayString contentLines = wxSplit(dirContent, '\n');
    SSH_LOG("Number of content lines: " << contentLines.size());
    
    int itemsAdded = 0;
    for (const wxString& line : contentLines) {
        if (line.IsEmpty() || line.StartsWith("total")) continue;
        
        // Use regex to parse ls -la output
        // Format: permissions links owner group size month day time name
        wxRegEx regex(wxT("^([drwx-]+)\\s+\\d+\\s+\\S+\\s+\\S+\\s+(\\d+)\\s+(\\w+)\\s+(\\d+)\\s+([\\d:]+)\\s+(.+)$"));
        
        if (regex.Matches(line)) {
            wxString permissions = regex.GetMatch(line, 1);
            wxString sizeStr = regex.GetMatch(line, 2);
            wxString month = regex.GetMatch(line, 3);
            wxString day = regex.GetMatch(line, 4);
            wxString time = regex.GetMatch(line, 5);
            wxString name = regex.GetMatch(line, 6);
            bool isDirectory = permissions.StartsWith("d");
            
            // Skip . and .. directories (.. is added manually)
            if (name == "." || name == "..") {
                continue;
            }
            
            SSH_LOG("Adding item: " << name);
            int iconIndex = isDirectory ? ICON_FOLDER : ICON_FILE;
            long index = m_remoteList->InsertItem(itemsAdded + 1, name, iconIndex);
            
            // Set size column (empty for directories)
            if (isDirectory) {
                m_remoteList->SetItem(index, 1, "");
            } else {
                long long size = 0;
                sizeStr.ToLongLong(&size);
                m_remoteList->SetItem(index, 1, FormatFileSize(size));
                // Store actual size in item data
                m_remoteList->SetItemData(index, size);
            }
            
            // Set date column (simplified format from ls -la)
            wxString dateStr = month + " " + day + " " + time;
            m_remoteList->SetItem(index, 2, dateStr);
            
            itemsAdded++;
        }
    }
    
    SSH_LOG("Total items added: " << itemsAdded);
    
    // Update path label (use full path if available)
    if (targetPath == "~" && !m_remoteCurrentPath.IsEmpty() && m_remoteCurrentPath != "~") {
        // Already have full path from pwd, don't override
        m_remotePathLabel->SetLabel(TranslationHelper::Tr("remoteDirectory") + m_remoteCurrentPath);
    } else {
        m_remotePathLabel->SetLabel(TranslationHelper::Tr("remoteDirectory") + targetPath);
        m_remoteCurrentPath = targetPath;
    }
    
    // Update drop target remote path
    if (m_remoteDropTarget) {
        m_remoteDropTarget->UpdateRemotePath(m_remoteCurrentPath);
    }
    
    SSH_LOG("OnSSHFileList called - END");
}

void FileTransferDialog::OnSSHCommandOutput(wxCommandEvent& event) {
    wxString output = event.GetString();
    SSH_LOG("OnSSHCommandOutput: " << output);
    
    // Trim whitespace
    output.Trim();
    output.Trim(false);
    
    // If this is a pwd output, update the remote current path
    if (!output.IsEmpty() && output.StartsWith("/")) {
        m_remoteCurrentPath = output;
        m_remotePathLabel->SetLabel(TranslationHelper::Tr("remoteDirectory") + m_remoteCurrentPath);
        SSH_LOG("Updated remote current path to: " << m_remoteCurrentPath);
        
        // Update drop target remote path
        if (m_remoteDropTarget) {
            m_remoteDropTarget->UpdateRemotePath(m_remoteCurrentPath);
        }
        
        // Also request the directory listing for the full path
        RequestRemoteDirectory(m_remoteCurrentPath);
    }
}

void FileTransferDialog::PopulateLocalList(const wxString& path) {
    if (!m_localList) {
        return;
    }
    
    m_localList->DeleteAllItems();

#ifdef _WIN32
    if (path.IsEmpty()) {
        // Show all drives
        DWORD drives = GetLogicalDrives();
        int itemIndex = 0;
        for (int i = 0; i < 26; i++) {
            if (drives & (1 << i)) {
                wxString driveLabel = wxString::Format("%c:", 'A' + i);
                long index = m_localList->InsertItem(itemIndex, driveLabel, ICON_FOLDER);
                m_localList->SetItem(index, 1, "");
                m_localList->SetItem(index, 2, "");
                itemIndex++;
            }
        }
        m_localPathLabel->SetLabel(TranslationHelper::Tr("localDirectory") + "Computer");
        m_localCurrentPath = wxEmptyString;
        return;
    }
#endif
    
    // Add parent directory entry
    bool showParent = false;
    wxFileName fn(path);
    if (fn.GetDirCount() > 0) {
        showParent = true;
    }
#ifdef _WIN32
    // On Windows, show ".." for drive roots to go back to drive list
    if (!path.IsEmpty() && path.Length() >= 2 && path[1] == ':') {
        showParent = true;
    }
#endif
    if (showParent) {
        long parentIndex = m_localList->InsertItem(0, "..", ICON_FOLDER);
        m_localList->SetItem(parentIndex, 1, "");
        m_localList->SetItem(parentIndex, 2, "");
    }
    
    wxDir dir(path);
    if (!dir.IsOpened()) {
        return;
    }
    
    wxString filename;
    bool hasFiles = dir.GetFirst(&filename, wxEmptyString, wxDIR_DIRS | wxDIR_FILES);
    
    int itemIndex = 1;
    while (hasFiles) {
        wxString fullPath = path + wxFileName::GetPathSeparator() + filename;
        bool isDirectory = wxDirExists(fullPath);
        int iconIndex = isDirectory ? ICON_FOLDER : ICON_FILE;
        
        long index = m_localList->InsertItem(itemIndex, filename, iconIndex);
        
        if (isDirectory) {
            m_localList->SetItem(index, 1, "");
        } else {
            wxFileName fn(fullPath);
            long long fileSize = fn.GetSize().GetValue();
            m_localList->SetItem(index, 1, FormatFileSize(fileSize));
        }
        
        // Get file modification time
        wxDateTime modTime = wxFileName(fullPath).GetModificationTime();
        if (modTime.IsValid()) {
            m_localList->SetItem(index, 2, FormatFileDate(modTime));
        } else {
            m_localList->SetItem(index, 2, "");
        }
        
        itemIndex++;
        hasFiles = dir.GetNext(&filename);
    }
    
    // Update path label
    m_localPathLabel->SetLabel(TranslationHelper::Tr("localDirectory") + path);
    m_localCurrentPath = path;
    
    // Update local drop target path
    if (m_localDropTarget) {
        m_localDropTarget->UpdateLocalPath(path);
    }
}

void FileTransferDialog::PopulateRemoteList() {
    // Remote list is populated lazily via SSH thread
    if (m_remoteList) {
        m_remoteList->DeleteAllItems();
        long index = m_remoteList->InsertItem(0, "Loading...", ICON_LOADING);
        m_remoteList->SetItem(index, 1, "");
    }
}

void FileTransferDialog::RequestRemoteDirectory(const wxString& path) {
    if (m_sshThread) {
        m_sshThread->RequestDirectory(path);
    }
}

wxString FileTransferDialog::FormatFileSize(long long bytes) {
    if (bytes < 1024) {
        return wxString::Format("%lld B", bytes);
    } else if (bytes < 1024 * 1024) {
        return wxString::Format("%.1f KB", bytes / 1024.0);
    } else if (bytes < 1024 * 1024 * 1024) {
        return wxString::Format("%.1f MB", bytes / (1024.0 * 1024.0));
    } else {
        return wxString::Format("%.1f GB", bytes / (1024.0 * 1024.0 * 1024.0));
    }
}

wxString FileTransferDialog::FormatFileDate(const wxDateTime& date) {
    return date.Format("%Y-%m-%d");
}

void FileTransferDialog::CreateControls() {
    // Create main panel
    wxPanel* mainPanel = new wxPanel(this, wxID_ANY);
    
    // Create local section
    m_localPathLabel = new wxStaticText(mainPanel, wxID_ANY, TranslationHelper::Tr("localDirectory") + m_localCurrentPath);
    
    m_localList = new wxListCtrl(mainPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                 wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_NO_HEADER);
    m_localList->AssignImageList(m_imageList, wxIMAGE_LIST_SMALL);
    
    // Add columns to local list
    m_localList->AppendColumn(TranslationHelper::Tr("name"), wxLIST_FORMAT_LEFT, 300);
    m_localList->AppendColumn(TranslationHelper::Tr("size"), wxLIST_FORMAT_RIGHT, 100);
    m_localList->AppendColumn(TranslationHelper::Tr("changed"), wxLIST_FORMAT_LEFT, 150);
    
    // Create remote section
    m_remotePathLabel = new wxStaticText(mainPanel, wxID_ANY, TranslationHelper::Tr("remoteDirectory") + m_remoteCurrentPath);
    
    m_remoteList = new wxListCtrl(mainPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                  wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_NO_HEADER);
    m_remoteList->AssignImageList(m_imageList, wxIMAGE_LIST_SMALL);
    
    // Set drop target for remote list (for upload from local)
    m_remoteDropTarget = new FileDropTarget(GetParent(), wxString(m_deviceConfig.id.c_str(), wxConvUTF8), m_remoteCurrentPath, false);
    m_remoteList->SetDropTarget(m_remoteDropTarget);
    
    // Set drop target for local list (for download from remote)
    m_localDropTarget = new FileDropTarget(GetParent(), wxString(m_deviceConfig.id.c_str(), wxConvUTF8), m_localCurrentPath, true);
    m_localList->SetDropTarget(m_localDropTarget);
    
    // Add columns to remote list
    m_remoteList->AppendColumn(TranslationHelper::Tr("name"), wxLIST_FORMAT_LEFT, 300);
    m_remoteList->AppendColumn(TranslationHelper::Tr("size"), wxLIST_FORMAT_RIGHT, 100);
    m_remoteList->AppendColumn(TranslationHelper::Tr("changed"), wxLIST_FORMAT_LEFT, 150);
    
    // Bind double-click event for local list (navigate into directory)
    m_localList->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent& event) {
        long index = event.GetIndex();
        wxString name = m_localList->GetItemText(index);
        wxString size = m_localList->GetItemText(index, 1);
        
        // Check if it's a directory (empty size) or parent directory
        if (size.IsEmpty() || name == "..") {
            wxString newPath;
            if (name == "..") {
                // Go to parent directory
#ifdef _WIN32
                if (m_localCurrentPath.Length() == 3 && m_localCurrentPath[1] == ':') {
                    // Drive root like C:\, go back to drive list
                    newPath = wxEmptyString;
                } else {
#endif
                    wxFileName fn(m_localCurrentPath);
                    fn.RemoveLastDir();
                    newPath = fn.GetFullPath();
                    // Ensure path ends with separator for directories
                    if (!newPath.IsEmpty() && newPath.Last() != wxFileName::GetPathSeparator()) {
                        newPath += wxFileName::GetPathSeparator();
                    }
#ifdef _WIN32
                }
#endif
            } else {
                // Go into subdirectory / drive
#ifdef _WIN32
                if (m_localCurrentPath.IsEmpty()) {
                    // Drive selection view
                    newPath = name + wxFileName::GetPathSeparator();
                } else {
#endif
                    // Go into subdirectory - use simple path concatenation
                    if (m_localCurrentPath.Last() == wxFileName::GetPathSeparator()) {
                        newPath = m_localCurrentPath + name;
                    } else {
                        newPath = m_localCurrentPath + wxFileName::GetPathSeparator() + name;
                    }
                    newPath += wxFileName::GetPathSeparator();
#ifdef _WIN32
                }
#endif
            }
            
            if (newPath != m_localCurrentPath) {
                m_localCurrentPath = newPath;
                wxString labelPath = m_localCurrentPath.IsEmpty() ? wxString("Computer") : m_localCurrentPath;
                m_localPathLabel->SetLabel(TranslationHelper::Tr("localDirectory") + labelPath);
                PopulateLocalList(m_localCurrentPath);
            }
        }
    });
    
    // Bind begin drag event to only allow dragging files (not directories)
    m_localList->Bind(wxEVT_LIST_BEGIN_DRAG, [this](wxListEvent& event) {
        long index = event.GetIndex();
        wxString name = m_localList->GetItemText(index);
        wxString size = m_localList->GetItemText(index, 1);
        
        // Only allow dragging if it's a file (size is not empty)
        if (size.IsEmpty()) {
            event.Veto();
            return;
        }
        
        // Create file path with proper separator
        wxString fullPath = m_localCurrentPath;
        if (!fullPath.EndsWith(wxFileName::GetPathSeparator())) {
            fullPath += wxFileName::GetPathSeparator();
        }
        fullPath += name;
        
        // Create file data object
        wxFileDataObject fileData;
        fileData.AddFile(fullPath);
        
        // Create drop source and start drag
        wxDropSource source(fileData, m_localList);
        wxDragResult result = source.DoDragDrop(wxDragCopy);
        
        event.Skip();
    });
    
    // Bind double-click event for remote list (navigate into directory)
    m_remoteList->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent& event) {
        long index = event.GetIndex();
        wxString name = m_remoteList->GetItemText(index);
        wxString size = m_remoteList->GetItemText(index, 1);
        
        // Check if it's a directory (empty size) or parent directory
        if (size.IsEmpty() || name == "..") {
            wxString newPath;
            if (name == "..") {
                // Go to parent directory
                if (m_remoteCurrentPath != "~" && m_remoteCurrentPath != "/") {
                    size_t lastSlash = m_remoteCurrentPath.rfind('/');
                    if (lastSlash != wxString::npos) {
                        newPath = m_remoteCurrentPath.substr(0, lastSlash);
                        if (newPath.IsEmpty()) newPath = "/";
                    }
                }
            } else {
                // Go into subdirectory
                // Avoid double slash
                if (m_remoteCurrentPath.EndsWith("/")) {
                    newPath = m_remoteCurrentPath + name;
                } else {
                    newPath = m_remoteCurrentPath + "/" + name;
                }
            }
            
            if (!newPath.IsEmpty()) {
                m_remoteCurrentPath = newPath;
                m_remotePathLabel->SetLabel(TranslationHelper::Tr("remoteDirectory") + m_remoteCurrentPath);
                RequestRemoteDirectory(m_remoteCurrentPath);
            }
        }
    });
    
    // Bind begin drag event for remote list (only allow dragging files for download)
    m_remoteList->Bind(wxEVT_LIST_BEGIN_DRAG, [this](wxListEvent& event) {
        long index = event.GetIndex();
        wxString name = m_remoteList->GetItemText(index);
        wxString size = m_remoteList->GetItemText(index, 1);
        
        // Only allow dragging if it's a file (size is not empty)
        if (size.IsEmpty()) {
            event.Veto();
            return;
        }
        
        // Create remote file path
        wxString remotePath = m_remoteCurrentPath;
        if (!remotePath.EndsWith("/")) {
            remotePath += "/";
        }
        remotePath += name;
        
        // Get actual file size in bytes from item data
        long long sizeBytes = m_remoteList->GetItemData(index);
        
        // Create file data object with remote path and size (format: "path|size")
        wxString dragData = remotePath + "|" + wxString::Format("%lld", sizeBytes);
        wxTextDataObject textData(dragData);
        
        // Create drop source and start drag
        wxDropSource source(textData, m_remoteList);
        wxDragResult result = source.DoDragDrop(wxDragCopy);
        
        event.Skip();
    });
    
    // Create task list (height for 4 rows)
    m_taskList = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 160),
                                wxLC_REPORT | wxLC_SINGLE_SEL);
    m_taskList->AppendColumn(TranslationHelper::Tr("action"), wxLIST_FORMAT_LEFT, 200);
    m_taskList->AppendColumn(TranslationHelper::Tr("local"), wxLIST_FORMAT_LEFT, 200);
    m_taskList->AppendColumn(TranslationHelper::Tr("remote"), wxLIST_FORMAT_LEFT, 200);
    m_taskList->AppendColumn(TranslationHelper::Tr("size"), wxLIST_FORMAT_RIGHT, 200);
    m_taskList->AppendColumn(TranslationHelper::Tr("progress"), wxLIST_FORMAT_LEFT, 200);
    m_taskList->AppendColumn(TranslationHelper::Tr("status"), wxLIST_FORMAT_LEFT, 150);
    m_taskList->AppendColumn(TranslationHelper::Tr("result"), wxLIST_FORMAT_LEFT, 200);
    
    // Layout main panel
    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
    
    // Local section
    wxBoxSizer* localSizer = new wxBoxSizer(wxVERTICAL);
    localSizer->Add(m_localPathLabel, 0, wxALL, 5);
    localSizer->Add(m_localList, 1, wxEXPAND | wxALL, 5);
    
    // Remote section
    wxBoxSizer* remoteSizer = new wxBoxSizer(wxVERTICAL);
    remoteSizer->Add(m_remotePathLabel, 0, wxALL, 5);
    remoteSizer->Add(m_remoteList, 1, wxEXPAND | wxALL, 5);
    
    // Split local and remote horizontally
    wxBoxSizer* splitSizer = new wxBoxSizer(wxHORIZONTAL);
    splitSizer->Add(localSizer, 1, wxEXPAND);
    splitSizer->Add(remoteSizer, 1, wxEXPAND);
    
    mainSizer->Add(splitSizer, 1, wxEXPAND);
    mainPanel->SetSizer(mainSizer);
    
    // Dialog layout
    wxBoxSizer* dialogSizer = new wxBoxSizer(wxVERTICAL);
    dialogSizer->Add(mainPanel, 1, wxEXPAND | wxALL, 5);
    dialogSizer->Add(m_taskList, 0, wxEXPAND | wxALL, 5);
    SetSizer(dialogSizer);
}

void FileTransferDialog::LayoutControls() {
    Layout();
    Centre();
    AdjustTaskListColumns();
    AdjustFileListColumns();
}

void FileTransferDialog::AdjustTaskListColumns() {
    if (!m_taskList) return;
    
    int totalWidth = m_taskList->GetClientSize().GetWidth();
    if (totalWidth <= 0) return;
    
    // Fixed widths for other columns
    int actionWidth = 200;
    int sizeWidth = 200;
    int progressWidth = 200;
    int statusWidth = 150;
    int resultWidth = 250;
    
    int fixedWidth = actionWidth + sizeWidth + progressWidth + statusWidth + resultWidth;
    int remainingWidth = totalWidth - fixedWidth;
    
    if (remainingWidth > 0) {
        int pathColumnWidth = remainingWidth / 2;
        m_taskList->SetColumnWidth(0, actionWidth);  // Action
        m_taskList->SetColumnWidth(1, pathColumnWidth);  // Local
        m_taskList->SetColumnWidth(2, pathColumnWidth);  // Remote
        m_taskList->SetColumnWidth(3, sizeWidth);  // Size
        m_taskList->SetColumnWidth(4, progressWidth);  // Progress
        m_taskList->SetColumnWidth(5, statusWidth);  // Status
        m_taskList->SetColumnWidth(6, resultWidth);  // Result
    }
}

void FileTransferDialog::AdjustFileListColumns() {
    if (!m_localList || !m_remoteList) return;
    
    int localWidth = m_localList->GetClientSize().GetWidth();
    int remoteWidth = m_remoteList->GetClientSize().GetWidth();
    
    if (localWidth > 0) {
        // Calculate proportional column widths for local list
        int sizeWidth = 120;
        int dateWidth = 140;
        int nameWidth = localWidth - sizeWidth - dateWidth - 40; // 40 for scrollbar padding
        
        if (nameWidth > 50) {
            m_localList->SetColumnWidth(0, nameWidth);  // Name
            m_localList->SetColumnWidth(1, sizeWidth);  // Size
            m_localList->SetColumnWidth(2, dateWidth);  // Changed
        }
    }
    
    if (remoteWidth > 0) {
        // Calculate proportional column widths for remote list
        int sizeWidth = 120;
        int dateWidth = 140;
        int nameWidth = remoteWidth - sizeWidth - dateWidth - 40; // 40 for scrollbar padding
        
        if (nameWidth > 50) {
            m_remoteList->SetColumnWidth(0, nameWidth);  // Name
            m_remoteList->SetColumnWidth(1, sizeWidth);  // Size
            m_remoteList->SetColumnWidth(2, dateWidth);  // Changed
        }
    }
}

void FileTransferDialog::OnTimer(wxTimerEvent& event) {
    LoadAndDisplayTasks();
}

void FileTransferDialog::OnSize(wxSizeEvent& event) {
    event.Skip();
    AdjustTaskListColumns();
    AdjustFileListColumns();
}

void FileTransferDialog::OnFileTransferProgress(wxCommandEvent& event) {
    wxString json = event.GetString();
    SSH_LOG("File Transfer Progress: " << json);
    
    try {
        nlohmann::json j = nlohmann::json::parse(json.ToStdString());
        std::string taskId = j["id"];
        int progress = j["progress"];
        std::string status = j["status"];
        
        // Update task list in real-time
        LoadAndDisplayTasks();
    } catch (const std::exception& e) {
        SSH_LOG("Failed to parse file transfer progress: " << e.what());
    }
}

void FileTransferDialog::OnFileTransferComplete(wxCommandEvent& event) {
    wxString json = event.GetString();
    SSH_LOG("File Transfer Complete: " << json);
    
    try {
        nlohmann::json j = nlohmann::json::parse(json.ToStdString());
        std::string taskId = j["id"];
        int progress = j["progress"];
        std::string status = j["status"];
        
        // Update task list to show completion
        LoadAndDisplayTasks();
    } catch (const std::exception& e) {
        SSH_LOG("Failed to parse file transfer complete: " << e.what());
    }
}

void FileTransferDialog::LoadAndDisplayTasks() {
    if (!m_taskList) {
        return;
    }
    
    // Get task file path
    wxString workspaceDir = wxString::FromUTF8(GlobalConfig::GetWorkspacePath().c_str());
    wxString deviceId = wxString::FromUTF8(m_deviceConfig.id.c_str());
    deviceId.Replace("/", "_"); // Replace slashes with underscores
    wxString deviceDir = workspaceDir + wxFileName::GetPathSeparator() + deviceId;
    wxString taskFilePath = deviceDir + wxFileName::GetPathSeparator() + "task.json";
    
    SSH_LOG("Loading task list from: " << taskFilePath.ToStdString());
    
    // Load task file
    std::ifstream file(taskFilePath.ToStdString());
    if (!file.is_open()) {
        SSH_LOG("Task file not found: " << taskFilePath.ToStdString());
        m_taskList->DeleteAllItems();
        return;
    }
    
    try {
        nlohmann::json j;
        file >> j;
        file.close();
        
        FileTransferTaskList taskList = FileTransferTaskList::fromJson(j);
        
        // Clear current list
        m_taskList->DeleteAllItems();
        
        // Add tasks to list (newest first - reverse order)
        for (auto it = taskList.tasks.rbegin(); it != taskList.tasks.rend(); ++it) {
            const auto& task = *it;
            wxString actionText;
            if (task.action == "download") {
                actionText = TranslationHelper::Tr("download");
            } else if (task.action == "upload") {
                actionText = TranslationHelper::Tr("upload");
            } else {
                actionText = wxString::FromUTF8(task.action.c_str());
            }
            long index = m_taskList->InsertItem(m_taskList->GetItemCount(), actionText);
            m_taskList->SetItem(index, 1, wxString::FromUTF8(task.local.c_str()));
            m_taskList->SetItem(index, 2, wxString::FromUTF8(task.remote.c_str()));
            m_taskList->SetItem(index, 3, FormatFileSize(task.size));
            m_taskList->SetItem(index, 4, wxString::Format("%d%%", task.progress));
            
            // Defensive: fallback for empty status
            wxString statusText = wxString::FromUTF8(task.status.c_str());
            if (statusText.IsEmpty()) {
                statusText = "pending";
                SSH_LOG("WARNING: Empty status for task " << task.id << ", defaulting to pending");
            }
            wxString resultText = wxString::FromUTF8(task.result.c_str());
            if (resultText.IsEmpty() && task.status == "completed") {
                resultText = "success";
            }
            
            m_taskList->SetItem(index, 5, TranslationHelper::Tr(statusText));
            m_taskList->SetItem(index, 6, TranslationHelper::Tr(resultText));
            
            // Set color based on status
            wxColour color;
            if (task.status == "completed") {
                if (task.result == "success") {
                    color = wxColour(0, 128, 0);  // Green
                } else {
                    color = wxColour(255, 0, 0);  // Red
                }
            } else if (task.status == "transferring") {
                color = wxColour(0, 0, 255);  // Blue
            } else {
                color = wxColour(128, 128, 128);  // Gray
            }
            m_taskList->SetItemTextColour(index, color);
        }
    } catch (const std::exception& e) {
        SSH_LOG("Failed to load task list: " << e.what());
    }
}
