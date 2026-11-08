#include "FileTransferDialog.h"
#include "AsynchronousSFTPTask.h"
#include "SSHManager.h"
#include "FileTransferTask.h"
#include "GlobalConfig.h"
#include "TranslationHelper.h"
#include "UiColors.h"
#include <wx/dir.h>
#include <wx/bmpbuttn.h>
#include <wx/imaglist.h>
#include <wx/filename.h>
#include <wx/regex.h>
#include <vector>
#include <wx/artprov.h>
#include <wx/datetime.h>
#include <wx/dnd.h>
#include <fstream>
#include <sstream>
#include <random>

wxDEFINE_EVENT(wxEVT_FILE_TRANSFER_REQUEST, wxCommandEvent);

namespace {

void AppendFileRow(wxDataViewListCtrl* list, const wxString& name,
                   const wxIcon& icon, const wxString& size,
                   const wxString& date) {
    wxVector<wxVariant> row;
    row.push_back(wxVariant(wxDataViewIconText(name, icon)));
    row.push_back(wxVariant(size));
    row.push_back(wxVariant(date));
    list->AppendItem(row);
}

// Column 0 holds wxDataViewIconText, not a plain string — GetTextValue would
// return empty on it. Extract the label from the variant.
wxString GetItemName(wxDataViewListCtrl* list, int row) {
    wxVariant value;
    list->GetValue(value, row, 0);
    wxDataViewIconText data;
    data << value;
    return data.GetText();
}

// The native SysHeader32 used by wxDataViewCtrl on wxMSW ignores colours, so
// each list gets a plain-panel header row whose labels mirror the column
// titles. Widths are synced in SyncListHeader.
const int kHeaderPad = 6;

wxPanel* CreateListHeader(wxWindow* parent, wxDataViewListCtrl* list) {
    const UiPalette pal = CurrentUiPalette();
    wxPanel* panel = new wxPanel(parent, wxID_ANY);
    panel->SetBackgroundColour(pal.bgHeader);
    wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);
    const unsigned count = list->GetColumnCount();
    for (unsigned i = 0; i < count; ++i) {
        wxStaticText* label = new wxStaticText(panel, wxID_ANY,
                                               list->GetColumn(i)->GetTitle());
        label->SetForegroundColour(pal.textFaint);
        label->SetFont(label->GetFont().Bold());
        sizer->Add(label, i + 1 == count ? 1 : 0,
                   wxLEFT | wxALIGN_CENTER_VERTICAL, kHeaderPad);
    }
    panel->SetSizer(sizer);
    panel->SetMinSize(wxSize(-1, list->GetCharHeight() + 10));
    return panel;
}

// Mirror the list's column widths onto the header labels so the two stay
// aligned when columns are resized by AdjustTaskListColumns et al.
void SyncListHeader(wxDataViewListCtrl* list, wxPanel* header) {
    if (!list || !header) return;
    const unsigned count = list->GetColumnCount();
    unsigned i = 0;
    for (wxWindow* child : header->GetChildren()) {
        if (i >= count) break;
        const int w = list->GetColumn(i)->GetWidth() - kHeaderPad;
        if (w > 0) child->SetMinSize(wxSize(w, -1));
        ++i;
    }
    header->Layout();
}

wxString GenerateUUID() {
    static std::mt19937 rng([]() {
        std::random_device rd;
        std::seed_seq seq{
            rd(), rd(), rd(), rd(),
            static_cast<unsigned int>(std::chrono::high_resolution_clock::now().time_since_epoch().count())
        };
        return std::mt19937(seq);
    }());
    std::uniform_int_distribution<int> dist(0, 15);

    wxString uuid;
    for (int i = 0; i < 32; i++) {
        if (i == 8 || i == 12 || i == 16 || i == 20) uuid += "-";
        int digit = dist(rng);
        uuid += digit < 10 ? wxString::Format("%d", digit)
                         : wxString::Format("%c", 'a' + (digit - 10));
    }
    return uuid;
}

} // namespace

FileTransferDialog::FileTransferDialog(wxWindow* parent, const wxString& title, const DeviceConfig& deviceConfig)
    : wxDialog(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      m_deviceConfig(deviceConfig),
      m_localList(nullptr), m_remoteList(nullptr), m_taskList(nullptr), m_localPathLabel(nullptr),
      m_remotePathLabel(nullptr),
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
    
    CreateIcons();
    CreateControls();
    LayoutControls();
    
    // Populate local list
    PopulateLocalList(m_localCurrentPath);
    
    // Get the full path of ~ by executing pwd, then request directory listing
    SSH_LOG("FileTransferDialog constructor - Getting full path with pwd");
    AsynchronousSFTPTask::CreateCommandTask(this, m_deviceConfig, "pwd");
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
}

void FileTransferDialog::CreateIcons() {
    const wxSize sz(16, 16);
    m_folderIcon = wxArtProvider::GetIcon(wxART_FOLDER, wxART_OTHER, sz);
    m_fileIcon = wxArtProvider::GetIcon(wxART_NORMAL_FILE, wxART_OTHER, sz);
    m_loadingIcon = wxArtProvider::GetIcon(wxART_EXECUTABLE_FILE, wxART_OTHER, sz);
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
    m_remoteFileSizes.clear();

    // Add parent directory entry
    AppendFileRow(m_remoteList, "..", m_folderIcon, "", "");
    m_remoteFileSizes.push_back(0);
    
    // Parse ls -la output
    wxArrayString contentLines = wxSplit(dirContent, '\n');
    SSH_LOG("Number of content lines: " << contentLines.size());
    
    int itemsAdded = 0;
    for (const wxString& line : contentLines) {
        if (line.IsEmpty() || line.StartsWith("total")) continue;
        
        // Use regex to parse ls -la output
        // Format: permissions links owner group size month day time name
        // Tolerant of: SELinux/ACL suffix (. + @), symlink 'l', non-ASCII
        // locale month names (e.g. '10月').
        wxRegEx regex(wxT("^([bcdlps-][a-zA-Z-]{9}[.+@]?)\\s+\\d+\\s+\\S+\\s+\\S+\\s+(\\d+)\\s+(\\S+)\\s+(\\S+)\\s+(\\S+)\\s+(.+)$"));
        
        if (regex.Matches(line)) {
            wxString permissions = regex.GetMatch(line, 1);
            wxString sizeStr = regex.GetMatch(line, 2);
            wxString month = regex.GetMatch(line, 3);
            wxString day = regex.GetMatch(line, 4);
            wxString time = regex.GetMatch(line, 5);
            wxString name = regex.GetMatch(line, 6);
            bool isDirectory = permissions.StartsWith("d");
            
            // Symlink entries display as "name -> target"; keep link name only
            if (permissions.StartsWith("l")) {
                size_t arrow = name.find(" -> ");
                if (arrow != wxString::npos) {
                    name = name.substr(0, arrow);
                }
            }
            
            // Skip . and .. directories (.. is added manually)
            if (name == "." || name == "..") {
                continue;
            }
            
            wxString displaySize;
            long long size = 0;
            if (!isDirectory) {
                sizeStr.ToLongLong(&size);
                displaySize = FormatFileSize(size);
            }
            wxString dateStr = month + " " + day + " " + time;
            AppendFileRow(m_remoteList, name,
                          isDirectory ? m_folderIcon : m_fileIcon,
                          displaySize, dateStr);
            m_remoteFileSizes.push_back(size);
            
            itemsAdded++;
        } else {
            SSH_LOG("OnSSHFileList: line did not match ls regex: '" << line << "'");
        }
    }
    
    SSH_LOG("Total items added: " << itemsAdded << " out of " << contentLines.size() << " lines");
    
    // Update path label (use full path if available)
    if (targetPath == "~" && !m_remoteCurrentPath.IsEmpty() && m_remoteCurrentPath != "~") {
        // Already have full path from pwd, don't override
        m_remotePathLabel->SetLabel(m_remoteCurrentPath);
    } else {
        m_remotePathLabel->SetLabel(targetPath);
        m_remoteCurrentPath = targetPath;
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
        m_remotePathLabel->SetLabel(m_remoteCurrentPath);
        SSH_LOG("Updated remote current path to: " << m_remoteCurrentPath);
        
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
                AppendFileRow(m_localList, driveLabel, m_folderIcon, "", "");
                itemIndex++;
            }
        }
        m_localPathLabel->SetLabel("Computer");
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
        AppendFileRow(m_localList, "..", m_folderIcon, "", "");
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

        wxString displaySize;
        if (!isDirectory) {
            wxFileName fn(fullPath);
            displaySize = FormatFileSize(fn.GetSize().GetValue());
        }

        wxString dateStr;
        wxDateTime modTime = wxFileName(fullPath).GetModificationTime();
        if (modTime.IsValid()) {
            dateStr = FormatFileDate(modTime);
        }

        AppendFileRow(m_localList, filename,
                      isDirectory ? m_folderIcon : m_fileIcon,
                      displaySize, dateStr);
        
        itemIndex++;
        hasFiles = dir.GetNext(&filename);
    }
    
    // Update path label
    m_localPathLabel->SetLabel(path);
    m_localCurrentPath = path;
    
}

void FileTransferDialog::PopulateRemoteList() {
    // Remote list is populated lazily via SSH thread
    if (m_remoteList) {
        m_remoteList->DeleteAllItems();
        m_remoteFileSizes.clear();
        AppendFileRow(m_remoteList, "Loading...", m_loadingIcon, "", "");
        m_remoteFileSizes.push_back(0);
    }
}

void FileTransferDialog::RequestRemoteDirectory(const wxString& path) {
    SSH_LOG("FileTransferDialog::RequestRemoteDirectory called for path='" << path
            << "' host=" << m_deviceConfig.username << "@" << m_deviceConfig.address
            << ":" << m_deviceConfig.port);
    PopulateRemoteList(); // Show "Loading..."
    AsynchronousSFTPTask::CreateListTask(this, m_deviceConfig, path);
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
    m_mainPanel = new wxPanel(this, wxID_ANY);
    wxPanel* mainPanel = m_mainPanel;
    mainPanel->SetBackgroundColour(CurrentUiPalette().bgWindow);

    auto makePathRow = [mainPanel](wxStaticText*& label, const wxString& path,
                                   std::function<void()> onRefresh) -> wxSizer* {
        label = new wxStaticText(mainPanel, wxID_ANY, path, wxDefaultPosition,
                                 wxDefaultSize, wxST_ELLIPSIZE_MIDDLE);
        label->SetFont(label->GetFont().Bold());
        label->SetForegroundColour(CurrentUiPalette().textPrimary);

        wxBitmapButton* refreshBtn = new wxBitmapButton(
            mainPanel, wxID_ANY,
            wxArtProvider::GetBitmap(wxART_REFRESH, wxART_BUTTON, wxSize(16, 16)),
            wxDefaultPosition, wxDefaultSize,
            wxBU_AUTODRAW | wxBORDER_NONE);
        refreshBtn->SetToolTip(TranslationHelper::Tr("refresh"));
        refreshBtn->Bind(wxEVT_BUTTON, [onRefresh](wxCommandEvent&) { onRefresh(); });

        wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
        row->Add(label, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, 4);
        row->Add(refreshBtn, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 4);
        return row;
    };

    // Create local section
    wxSizer* localPathRow = makePathRow(m_localPathLabel, m_localCurrentPath,
                                        [this]() { PopulateLocalList(m_localCurrentPath); });

    m_localList = new wxDataViewListCtrl(mainPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                         wxDV_ROW_LINES | wxDV_SINGLE | wxDV_NO_HEADER);
    m_localList->SetBackgroundColour(CurrentUiPalette().bgContent);
    m_localList->SetForegroundColour(CurrentUiPalette().textPrimary);
    m_localList->AppendIconTextColumn(TranslationHelper::Tr("name"),
                                      wxDATAVIEW_CELL_INERT, -1, wxALIGN_LEFT,
                                      wxDATAVIEW_COL_RESIZABLE);
    m_localList->AppendTextColumn(TranslationHelper::Tr("size"),
                                  wxDATAVIEW_CELL_INERT, 60, wxALIGN_RIGHT,
                                  wxDATAVIEW_COL_RESIZABLE);
    m_localList->AppendTextColumn(TranslationHelper::Tr("changed"),
                                  wxDATAVIEW_CELL_INERT, 93, wxALIGN_LEFT,
                                  wxDATAVIEW_COL_RESIZABLE);
    
    // Create remote section
    wxSizer* remotePathRow = makePathRow(m_remotePathLabel, m_remoteCurrentPath,
                                         [this]() { RequestRemoteDirectory(m_remoteCurrentPath); });

    m_remoteList = new wxDataViewListCtrl(mainPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                          wxDV_ROW_LINES | wxDV_SINGLE | wxDV_NO_HEADER);
    m_remoteList->SetBackgroundColour(CurrentUiPalette().bgContent);
    m_remoteList->SetForegroundColour(CurrentUiPalette().textPrimary);
    
    // Drag & drop: local files -> remote list = upload; remote "path|size"
    // text -> local list = download. wxDataViewCtrl needs explicit
    // EnableDragSource/EnableDropTarget for the given formats.
    m_localList->EnableDragSource(wxDF_FILENAME);
    m_remoteList->EnableDragSource(wxDF_TEXT);
    m_remoteList->EnableDropTarget(wxDF_FILENAME);
    m_localList->EnableDropTarget(wxDF_TEXT);

    auto queueTransfer = [this](const wxString& localPath, const wxString& remotePath,
                                const wxString& action, long long size) {
        nlohmann::json j;
        j["id"] = GenerateUUID().ToStdString();
        j["device_id"] = m_deviceConfig.id;
        j["create_date"] = wxDateTime::Now().FormatISOCombined().ToStdString();
        j["local"] = localPath.ToStdString();
        j["remote"] = remotePath.ToStdString();
        j["action"] = action.ToStdString();
        j["size"] = size;
        wxCommandEvent event(wxEVT_FILE_TRANSFER_REQUEST);
        event.SetString(wxString(j.dump().c_str(), wxConvUTF8));
        wxQueueEvent(GetParent(), event.Clone());
    };

    // Upload: OS file dropped onto the remote list.
    m_remoteList->Bind(wxEVT_DATAVIEW_ITEM_DROP_POSSIBLE, [](wxDataViewEvent& e) {
        if (e.GetDataFormat() == wxDF_FILENAME) e.SetDropEffect(wxDragCopy);
    });
    m_remoteList->Bind(wxEVT_DATAVIEW_ITEM_DROP,
        [this, queueTransfer](wxDataViewEvent& e) {
            if (e.GetDataFormat() != wxDF_FILENAME) return;
            wxFileDataObject data;
            data.SetData(wxDF_FILENAME, e.GetDataSize(), e.GetDataBuffer());
            const wxArrayString& files = data.GetFilenames();
            if (files.IsEmpty()) return;

            wxString localPath = files[0];
            wxFileName fn(localPath);
            wxString remotePath = m_remoteCurrentPath;
            if (!remotePath.EndsWith("/")) remotePath += "/";
            remotePath += fn.GetFullName();
            long long size = 0;
            if (fn.FileExists()) size = fn.GetSize().GetValue();
            SSH_LOG("Queue upload: local=" << localPath << " remote=" << remotePath << " size=" << size);
            queueTransfer(localPath, remotePath, "upload", size);
        });

    // Download: remote "path|size" text dropped onto the local list.
    // Accept both TEXT and UNICODETEXT — macOS delivers the latter.
    m_localList->EnableDropTarget(wxDF_UNICODETEXT);
    m_localList->Bind(wxEVT_DATAVIEW_ITEM_DROP_POSSIBLE, [](wxDataViewEvent& e) {
        if (e.GetDataFormat() == wxDF_TEXT || e.GetDataFormat() == wxDF_UNICODETEXT)
            e.SetDropEffect(wxDragCopy);
    });
    m_localList->Bind(wxEVT_DATAVIEW_ITEM_DROP,
        [this, queueTransfer](wxDataViewEvent& e) {
            SSH_LOG("Drop on local list: format=" << e.GetDataFormat().GetType()
                    << " size=" << e.GetDataSize());
            if (e.GetDataFormat() != wxDF_TEXT &&
                e.GetDataFormat() != wxDF_UNICODETEXT) return;
            wxTextDataObject data;
            data.SetData(e.GetDataFormat(), e.GetDataSize(), e.GetDataBuffer());
            wxString dragData = data.GetText();
            SSH_LOG("Drop text: '" << dragData << "'");

            wxString remotePath = dragData;
            long long fileSize = 0;
            size_t pipePos = dragData.find('|');
            if (pipePos != wxString::npos && pipePos + 1 < dragData.length()) {
                remotePath = dragData.substr(0, pipePos);
                dragData.substr(pipePos + 1).ToLongLong(&fileSize);
            }

            wxString localPath = m_localCurrentPath;
            if (localPath.IsEmpty()) {
                localPath = wxGetHomeDir();
            }
            size_t lastSlash = remotePath.rfind('/');
            if (lastSlash != wxString::npos && lastSlash + 1 < remotePath.length()) {
                wxString fileName = remotePath.substr(lastSlash + 1);
                if (!localPath.EndsWith(wxFileName::GetPathSeparator())) {
                    localPath += wxFileName::GetPathSeparator();
                }
                localPath += fileName;
            }
            SSH_LOG("Queue download: local=" << localPath << " remote=" << remotePath);
            queueTransfer(localPath, remotePath, "download", fileSize);
        });
    
    // Add columns to remote list
    m_remoteList->AppendIconTextColumn(TranslationHelper::Tr("name"),
                                       wxDATAVIEW_CELL_INERT, -1, wxALIGN_LEFT,
                                       wxDATAVIEW_COL_RESIZABLE);
    m_remoteList->AppendTextColumn(TranslationHelper::Tr("size"),
                                   wxDATAVIEW_CELL_INERT, 60, wxALIGN_RIGHT,
                                   wxDATAVIEW_COL_RESIZABLE);
    m_remoteList->AppendTextColumn(TranslationHelper::Tr("changed"),
                                   wxDATAVIEW_CELL_INERT, 93, wxALIGN_LEFT,
                                   wxDATAVIEW_COL_RESIZABLE);
    
    // Bind double-click event for local list (navigate into directory)
    m_localList->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, [this](wxDataViewEvent& event) {
        int index = m_localList->ItemToRow(event.GetItem());
        if (index < 0) return;
        wxString name = GetItemName(m_localList, index);
        wxString size = m_localList->GetTextValue(index, 1);
        
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
                m_localPathLabel->SetLabel(labelPath);
                PopulateLocalList(m_localCurrentPath);
            }
        }
    });
    
    // Bind begin drag event to only allow dragging files (not directories).
    // A data object must be provided or the drag does not start.
    m_localList->Bind(wxEVT_DATAVIEW_ITEM_BEGIN_DRAG, [this](wxDataViewEvent& event) {
        int index = m_localList->ItemToRow(event.GetItem());
        if (index < 0) return;
        wxString name = GetItemName(m_localList, index);
        wxString size = m_localList->GetTextValue(index, 1);

        // Only allow dragging if it's a file (size is not empty)
        if (size.IsEmpty()) {
            return;
        }

        // Create file path with proper separator
        wxString fullPath = m_localCurrentPath;
        if (!fullPath.EndsWith(wxFileName::GetPathSeparator())) {
            fullPath += wxFileName::GetPathSeparator();
        }
        fullPath += name;

        wxFileDataObject* fileData = new wxFileDataObject();
        fileData->AddFile(fullPath);
        event.SetDataObject(fileData);
    });
    
    // Bind double-click event for remote list (navigate into directory)
    m_remoteList->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, [this](wxDataViewEvent& event) {
        int index = m_remoteList->ItemToRow(event.GetItem());
        if (index < 0) return;
        wxString name = GetItemName(m_remoteList, index);
        wxString size = m_remoteList->GetTextValue(index, 1);
        
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
                m_remotePathLabel->SetLabel(m_remoteCurrentPath);
                RequestRemoteDirectory(m_remoteCurrentPath);
            }
        }
    });
    
    // Bind begin drag event for remote list (only allow dragging files for download)
    m_remoteList->Bind(wxEVT_DATAVIEW_ITEM_BEGIN_DRAG, [this](wxDataViewEvent& event) {
        int index = m_remoteList->ItemToRow(event.GetItem());
        if (index < 0) return;
        wxString name = GetItemName(m_remoteList, index);
        wxString size = m_remoteList->GetTextValue(index, 1);

        // Only allow dragging if it's a file (size is not empty)
        if (size.IsEmpty()) {
            return;
        }

        // Create remote file path
        wxString remotePath = m_remoteCurrentPath;
        if (!remotePath.EndsWith("/")) {
            remotePath += "/";
        }
        remotePath += name;

        // Actual file size in bytes tracked in parallel to the rows
        long long sizeBytes =
            index < (int)m_remoteFileSizes.size() ? m_remoteFileSizes[index] : 0;

        // Drag data encodes remote path and size (format: "path|size")
        wxString dragData = remotePath + "|" + wxString::Format("%lld", sizeBytes);
        event.SetDataObject(new wxTextDataObject(dragData));
    });
    
    // Create task list (height for 4 rows). The native column headers are
    // hidden (wxDV_NO_HEADER) — SysHeader32 ignores colours on wxMSW, so a
    // plain themed panel is used for the header row instead.
    m_taskList = new wxDataViewListCtrl(this, wxID_ANY, wxDefaultPosition,
                                        wxSize(-1, 160), wxDV_ROW_LINES | wxDV_SINGLE | wxDV_NO_HEADER);
    m_taskList->SetBackgroundColour(CurrentUiPalette().bgContent);
    m_taskList->SetForegroundColour(CurrentUiPalette().textPrimary);
    m_taskList->AppendTextColumn(TranslationHelper::Tr("action"), wxDATAVIEW_CELL_INERT, -1);
    m_taskList->AppendTextColumn(TranslationHelper::Tr("local"), wxDATAVIEW_CELL_INERT, -1);
    m_taskList->AppendTextColumn(TranslationHelper::Tr("remote"), wxDATAVIEW_CELL_INERT, -1);
    m_taskList->AppendTextColumn(TranslationHelper::Tr("size"), wxDATAVIEW_CELL_INERT, -1);
    m_taskList->AppendTextColumn(TranslationHelper::Tr("progress"), wxDATAVIEW_CELL_INERT, -1);
    m_taskList->AppendTextColumn(TranslationHelper::Tr("status"), wxDATAVIEW_CELL_INERT, -1);
    m_taskList->AppendTextColumn(TranslationHelper::Tr("result"), wxDATAVIEW_CELL_INERT, -1);

    // Themed header panels stand in for the hidden native headers.
    m_localHeaderPanel = CreateListHeader(mainPanel, m_localList);
    m_remoteHeaderPanel = CreateListHeader(mainPanel, m_remoteList);
    m_taskHeaderPanel = CreateListHeader(this, m_taskList);

    // Layout main panel
    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

    // Local section
    wxStaticBoxSizer* localBox = new wxStaticBoxSizer(
        wxVERTICAL, mainPanel, TranslationHelper::Tr("localDirectory"));
    localBox->Add(localPathRow, 0, wxEXPAND | wxALL, 4);
    localBox->Add(m_localHeaderPanel, 0, wxEXPAND | wxLEFT | wxRIGHT, 4);
    localBox->Add(m_localList, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);

    // Remote section
    wxStaticBoxSizer* remoteBox = new wxStaticBoxSizer(
        wxVERTICAL, mainPanel, TranslationHelper::Tr("remoteDirectory"));
    remoteBox->Add(remotePathRow, 0, wxEXPAND | wxALL, 4);
    remoteBox->Add(m_remoteHeaderPanel, 0, wxEXPAND | wxLEFT | wxRIGHT, 4);
    remoteBox->Add(m_remoteList, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);

    // Split local and remote horizontally
    wxBoxSizer* splitSizer = new wxBoxSizer(wxHORIZONTAL);
    splitSizer->Add(localBox, 1, wxEXPAND | wxRIGHT, 4);
    splitSizer->Add(remoteBox, 1, wxEXPAND | wxLEFT, 4);

    mainSizer->Add(splitSizer, 1, wxEXPAND | wxALL, 8);
    mainPanel->SetSizer(mainSizer);

    // Task list section
    wxStaticBoxSizer* taskBox = new wxStaticBoxSizer(
        wxVERTICAL, this, TranslationHelper::Tr("transferTasks"));
    taskBox->Add(m_taskHeaderPanel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 4);
    taskBox->Add(m_taskList, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);

    // Dialog layout
    wxBoxSizer* dialogSizer = new wxBoxSizer(wxVERTICAL);
    dialogSizer->Add(mainPanel, 1, wxEXPAND);
    dialogSizer->Add(taskBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
    SetSizer(dialogSizer);

    // Re-apply theme colours when the OS appearance changes while open.
    Bind(wxEVT_SYS_COLOUR_CHANGED, [this](wxSysColourChangedEvent& event) {
        event.Skip();
        ApplyUiPalette();
        Refresh();
    });

    // The event is unreliable on wxMSW and the dialog sits outside the main
    // window's subtree, so also poll the effective appearance directly.
    m_appWasDark = SystemUsesDarkTheme();
    m_appearanceTimer.SetOwner(this);
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
        const bool dark = SystemUsesDarkTheme();
        if (dark != m_appWasDark) {
            m_appWasDark = dark;
            ApplyUiPalette();
            Refresh();
        }
    }, m_appearanceTimer.GetId());
    m_appearanceTimer.Start(2000);

    // The dialog object is created once and reused — re-apply the palette
    // every time it is shown so colours cached at construction can't go stale.
    Bind(wxEVT_SHOW, [this](wxShowEvent& event) {
        event.Skip();
        if (event.IsShown()) {
            m_appWasDark = SystemUsesDarkTheme();
            ApplyUiPalette();
        }
    });
}

void FileTransferDialog::ApplyUiPalette() {
    const UiPalette pal = CurrentUiPalette();
    ApplyDarkTitleBar(this);
    SetBackgroundColour(pal.bgWindow);
    if (m_mainPanel) m_mainPanel->SetBackgroundColour(pal.bgWindow);
    if (m_localPathLabel) m_localPathLabel->SetForegroundColour(pal.textPrimary);
    if (m_remotePathLabel) m_remotePathLabel->SetForegroundColour(pal.textPrimary);
    for (wxDataViewListCtrl* list : {m_localList, m_remoteList, m_taskList}) {
        if (!list) continue;
        list->SetBackgroundColour(pal.bgContent);
        list->SetForegroundColour(pal.textPrimary);
        list->Refresh();
    }
    // Group-box captions and stray labels use system colours — recolour them.
    // wxStaticBox is its own class (not a wxStaticText), so check both.
    std::function<void(wxWindow*)> recolor = [&](wxWindow* w) {
        if (auto* t = dynamic_cast<wxStaticText*>(w)) t->SetForegroundColour(pal.textPrimary);
        if (auto* b = dynamic_cast<wxStaticBox*>(w)) b->SetForegroundColour(pal.textPrimary);
        if (auto* btn = dynamic_cast<wxButton*>(w)) {
            btn->SetBackgroundColour(pal.tabHover);
            btn->SetForegroundColour(pal.textPrimary);
        }
        for (wxWindow* c : w->GetChildren()) recolor(c);
    };
    recolor(this);
    // Header panels: bg + muted label colour — applied after recolor() which
    // would otherwise set every wxStaticText to textPrimary.
    for (wxPanel* header : {m_localHeaderPanel, m_remoteHeaderPanel, m_taskHeaderPanel}) {
        if (!header) continue;
        header->SetBackgroundColour(pal.bgHeader);
        for (wxWindow* c : header->GetChildren())
            c->SetForegroundColour(pal.textFaint);
        header->Refresh();
    }
    // Native controls keep stale pixels until invalidated — repaint the whole
    // subtree so nothing lags one theme behind.
    RepaintUiTree(this);
    Update();
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
    
    // Fixed-width columns: Action / Size / Progress / Status / Result, each
    // sized to ~10 characters of the list font.
    int actionWidth = m_taskList->GetTextExtent("0123456789").x;
    int sizeWidth = actionWidth;
    int progressWidth = actionWidth;
    int statusWidth = actionWidth;
    int resultWidth = actionWidth;
    
    int fixedWidth = actionWidth + sizeWidth + progressWidth + statusWidth + resultWidth;
    int remainingWidth = totalWidth - fixedWidth;
    
    if (remainingWidth > 0) {
        int pathColumnWidth = remainingWidth / 2;
        const int widths[] = {actionWidth, pathColumnWidth, pathColumnWidth,
                              sizeWidth, progressWidth, statusWidth, resultWidth};
        for (int i = 0; i < 7; ++i) {
            if (wxDataViewColumn* col = m_taskList->GetColumn(i)) {
                col->SetWidth(widths[i]);
            }
        }
    }
    SyncListHeader(m_taskList, m_taskHeaderPanel);
}

void FileTransferDialog::AdjustFileListColumns() {
    if (!m_localList || !m_remoteList) return;
    
    int localWidth = m_localList->GetClientSize().GetWidth();
    int remoteWidth = m_remoteList->GetClientSize().GetWidth();
    
    auto adjust = [](wxDataViewListCtrl* list, wxPanel* header, int totalWidth) {
        if (totalWidth <= 0) return;
        int sizeWidth = 60;
        int dateWidth = 93;
        int nameWidth = totalWidth - sizeWidth - dateWidth - 40;
        if (nameWidth > 50) {
            list->GetColumn(0)->SetWidth(nameWidth);
            list->GetColumn(1)->SetWidth(sizeWidth);
            list->GetColumn(2)->SetWidth(dateWidth);
        }
        SyncListHeader(list, header);
    };
    adjust(m_localList, m_localHeaderPanel, localWidth);
    adjust(m_remoteList, m_remoteHeaderPanel, remoteWidth);
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
    // Progress events are deduplicated to fire only when percentage or state changes.
    LoadAndDisplayTasks();
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
    
    std::ifstream file(taskFilePath.ToStdString());
    if (!file.is_open()) {
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
            // Action column: arrow glyph + label (icons can't follow the palette)
            wxString actionText;
            if (task.action == "upload") {
                actionText = wxString::FromUTF8("\xE2\x86\x91 ") + TranslationHelper::Tr("upload");
            } else if (task.action == "download") {
                actionText = wxString::FromUTF8("\xE2\x86\x93 ") + TranslationHelper::Tr("download");
            } else {
                actionText = wxString::FromUTF8(task.action.c_str());
            }

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

            // Result column: tick/cross glyph for completed tasks
            wxString resultCell;
            if (task.status == "completed") {
                resultCell = (task.result == "success" || resultText == "success")
                                 ? wxString::FromUTF8("\xE2\x9C\x93")
                                 : wxString::FromUTF8("\xE2\x9C\x97");
            } else {
                resultCell = TranslationHelper::Tr(resultText);
            }

            wxVector<wxVariant> cells;
            cells.push_back(actionText);
            cells.push_back(wxString::FromUTF8(task.local.c_str()));
            cells.push_back(wxString::FromUTF8(task.remote.c_str()));
            cells.push_back(FormatFileSize(task.size));
            cells.push_back(wxString::Format("%d%%", task.progress));
            cells.push_back(TranslationHelper::Tr(statusText));
            cells.push_back(resultCell);
            m_taskList->AppendItem(cells);
        }
    } catch (const std::exception& e) {
        SSH_LOG("Failed to load task list: " << e.what());
    }
}
