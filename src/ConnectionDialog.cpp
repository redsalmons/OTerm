#include "ConnectionDialog.h"
#include "TranslationHelper.h"
#include "SSHManager.h"
#include "GlobalConfig.h"
#include <wx/filedlg.h>
#include <wx/file.h>

#ifdef __WXMSW__
#include <commctrl.h>
#endif

ConnectionDialog::ConnectionDialog(wxWindow* parent, const wxString& title, bool disableConnect)
    : wxDialog(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize) {

    double dpiScale = GlobalConfig::GetDPIScaleFactor();

    int baseWidth = 550;
    int baseHeight = 500;
    SetSize(wxSize(static_cast<int>(baseWidth * dpiScale), static_cast<int>(baseHeight * dpiScale)));

    wxBoxSizer* mainSizer = new wxBoxSizer(wxHORIZONTAL);

    // Left Panel (Tree Control) - let sizer handle sizing dynamically
    m_treeCtrl = new wxTreeCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                wxTR_DEFAULT_STYLE | wxTR_HAS_BUTTONS | wxTR_LINES_AT_ROOT | wxTR_SINGLE | wxTR_FULL_ROW_HIGHLIGHT);
    mainSizer->Add(m_treeCtrl, 1, wxEXPAND | wxALL, 5);

    // Right Panel (Form)
    wxPanel* formPanel = new wxPanel(this, wxID_ANY);
    wxBoxSizer* formSizer = new wxBoxSizer(wxVERTICAL);

    wxFlexGridSizer* gridSizer = new wxFlexGridSizer(4, 5, 5);
    gridSizer->Add(new wxStaticText(formPanel, wxID_ANY, TranslationHelper::Tr("deviceName") + ":"), 0, wxALIGN_CENTER_VERTICAL);
    m_nameCtrl = new wxTextCtrl(formPanel, wxID_ANY, "", wxDefaultPosition, wxSize(static_cast<int>(200 * dpiScale), -1));
    gridSizer->Add(m_nameCtrl, 0, wxALIGN_CENTER_VERTICAL);
    gridSizer->AddSpacer(0);
    gridSizer->AddSpacer(0);

    gridSizer->Add(new wxStaticText(formPanel, wxID_ANY, TranslationHelper::Tr("username") + ":"), 0, wxALIGN_CENTER_VERTICAL);
    m_usernameCtrl = new wxTextCtrl(formPanel, wxID_ANY, "", wxDefaultPosition, wxSize(static_cast<int>(200 * dpiScale), -1));
    gridSizer->Add(m_usernameCtrl, 0, wxALIGN_CENTER_VERTICAL);
    gridSizer->AddSpacer(0);
    gridSizer->AddSpacer(0);

    // Address field
    gridSizer->Add(new wxStaticText(formPanel, wxID_ANY, TranslationHelper::Tr("hostAddress") + ":"), 0, wxALIGN_CENTER_VERTICAL);
    m_addressCtrl = new wxTextCtrl(formPanel, wxID_ANY, "", wxDefaultPosition, wxSize(static_cast<int>(200 * dpiScale), -1));
    gridSizer->Add(m_addressCtrl, 0, wxALIGN_CENTER_VERTICAL);
    gridSizer->AddSpacer(0);
    gridSizer->AddSpacer(0);

    // Port field
    gridSizer->Add(new wxStaticText(formPanel, wxID_ANY, TranslationHelper::Tr("port") + ":"), 0, wxALIGN_CENTER_VERTICAL);
    m_portCtrl = new wxTextCtrl(formPanel, wxID_ANY, "22");
    m_portCtrl->SetMinSize(wxSize(static_cast<int>(60 * dpiScale), -1));
    gridSizer->Add(m_portCtrl, 0, wxALIGN_CENTER_VERTICAL);
    gridSizer->AddSpacer(0);
    gridSizer->AddSpacer(0);

    gridSizer->Add(new wxStaticText(formPanel, wxID_ANY, TranslationHelper::Tr("deviceGroup") + ":"), 0, wxALIGN_CENTER_VERTICAL);
    m_groupCtrl = new wxTextCtrl(formPanel, wxID_ANY, "", wxDefaultPosition, wxSize(static_cast<int>(200 * dpiScale), -1));
    gridSizer->Add(m_groupCtrl, 0, wxALIGN_CENTER_VERTICAL);
    gridSizer->AddSpacer(0);
    gridSizer->AddSpacer(0);

    gridSizer->Add(new wxStaticText(formPanel, wxID_ANY, TranslationHelper::Tr("authMethod") + ":"), 0, wxALIGN_CENTER_VERTICAL);
    wxString authChoices[] = { TranslationHelper::Tr("password"), TranslationHelper::Tr("key") };
    m_authChoice = new wxChoice(formPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, 2, authChoices);
    m_authChoice->SetSelection(0);
    gridSizer->Add(m_authChoice, 0);
    gridSizer->AddSpacer(0);
    gridSizer->AddSpacer(0);

    gridSizer->Add(new wxStaticText(formPanel, wxID_ANY, TranslationHelper::Tr("passwordKey") + ":"), 0, wxALIGN_CENTER_VERTICAL);
    wxBoxSizer* passwordSizer = new wxBoxSizer(wxHORIZONTAL);
    m_passwordCtrl = new wxTextCtrl(formPanel, wxID_ANY, "", wxDefaultPosition, wxSize(static_cast<int>(200 * dpiScale), -1), wxTE_PASSWORD);
    passwordSizer->Add(m_passwordCtrl, 0, wxALIGN_CENTER_VERTICAL);
    m_keyTextCtrl = new wxTextCtrl(formPanel, wxID_ANY, "", wxDefaultPosition, wxSize(static_cast<int>(200 * dpiScale), -1), wxTE_PASSWORD | wxTE_MULTILINE | wxTE_DONTWRAP);
    m_keyTextCtrl->Hide();
    passwordSizer->Add(m_keyTextCtrl, 0, wxALIGN_CENTER_VERTICAL);
    m_keyBrowseButton = new wxButton(formPanel, wxID_ANY, TranslationHelper::Tr("browse"));
    m_keyBrowseButton->Hide();
    passwordSizer->Add(m_keyBrowseButton, 0, wxLEFT, 5);
    gridSizer->Add(passwordSizer, 1, wxEXPAND);
    gridSizer->AddSpacer(0);
    gridSizer->AddSpacer(0);

    gridSizer->AddGrowableCol(1, 1);
    formSizer->Add(gridSizer, 1, wxEXPAND | wxALL, 12);

    wxBoxSizer* buttonSizer = new wxBoxSizer(wxHORIZONTAL);
    buttonSizer->Add(new wxButton(formPanel, wxID_SAVE, TranslationHelper::Tr("save")), 0, wxALL, 5);
    m_connectButton = new wxButton(formPanel, wxID_OK, TranslationHelper::Tr("connect"));
    m_connectButton->Enable(false);
    if (disableConnect) {
        m_connectButton->Hide();
    }
    buttonSizer->Add(m_connectButton, 0, wxALL, 5);
    buttonSizer->Add(new wxButton(formPanel, wxID_CANCEL, TranslationHelper::Tr("cancel")), 0, wxALL, 5);
    formSizer->Add(buttonSizer, 0, wxALIGN_RIGHT | wxALL, 10);

    formPanel->SetSizer(formSizer);
    mainSizer->Add(formPanel, 1, wxEXPAND | wxALL, 12);

    SetSizer(mainSizer);
    Centre();

    LoadConfig();
    RebuildTree();

    Bind(wxEVT_TREE_SEL_CHANGED, &ConnectionDialog::OnTreeSelectionChanged, this, m_treeCtrl->GetId());
    Bind(wxEVT_BUTTON, &ConnectionDialog::OnSave, this, wxID_SAVE);
    Bind(wxEVT_BUTTON, &ConnectionDialog::OnConnect, this, wxID_OK);
    Bind(wxEVT_BUTTON, &ConnectionDialog::OnCancel, this, wxID_CANCEL);
    Bind(wxEVT_CHOICE, &ConnectionDialog::OnAuthMethodChanged, this, m_authChoice->GetId());
    Bind(wxEVT_BUTTON, &ConnectionDialog::OnKeyBrowse, this, m_keyBrowseButton->GetId());
    Bind(wxEVT_SIZE, &ConnectionDialog::OnSize, this);
}

void ConnectionDialog::LoadConfig() {
    m_devices = DeviceConfig::LoadFromFile();
}

void ConnectionDialog::SaveConfig() {
    DeviceConfig::SaveToFile(m_devices);
}

void ConnectionDialog::RebuildTree() {
    m_treeCtrl->DeleteAllItems();
    m_rootId = m_treeCtrl->AddRoot(TranslationHelper::Tr("devices"));

    std::map<std::string, wxTreeItemId> pathItems;

    for (size_t i = 0; i < m_devices.size(); ++i) {
        const auto& dev = m_devices[i];
        wxTreeItemId parentId = m_rootId;
        if (!dev.group.empty()) {
            // Split group by "/" to create hierarchical structure
            std::string group = dev.group;
            std::string path;
            size_t pos = 0;
            while ((pos = group.find('/')) != std::string::npos) {
                std::string part = group.substr(0, pos);
                if (!path.empty()) path += "/";
                path += part;

                if (pathItems.find(path) == pathItems.end()) {
                    pathItems[path] = m_treeCtrl->AppendItem(parentId, part);
                }
                parentId = pathItems[path];
                // Safely erase: ensure we don't exceed string length
                if (pos + 1 <= group.length()) {
                    group.erase(0, pos + 1);
                } else {
                    group.clear();
                }
            }
            // Add the last part
            if (!group.empty()) {
                if (!path.empty()) path += "/";
                path += group;
                if (pathItems.find(path) == pathItems.end()) {
                    pathItems[path] = m_treeCtrl->AppendItem(parentId, group);
                }
                parentId = pathItems[path];
            }
        }
        m_treeCtrl->AppendItem(parentId, dev.name, -1, -1, new DeviceTreeItemData(i));
    }
    m_treeCtrl->ExpandAll();
}

void ConnectionDialog::PopulateForm(const DeviceConfig& device) {
    m_nameCtrl->SetValue(device.name);
    m_usernameCtrl->SetValue(device.username);
    m_addressCtrl->SetValue(device.address);
    m_portCtrl->SetValue(device.port);
    m_groupCtrl->SetValue(device.group);
    m_authChoice->SetStringSelection(device.auth_method == "password" ? TranslationHelper::Tr("password") : TranslationHelper::Tr("key"));
    m_passwordCtrl->SetValue(device.password);
    m_keyTextCtrl->SetValue(device.password);
    UpdatePasswordFieldVisibility();
}

void ConnectionDialog::OnTreeSelectionChanged(wxTreeEvent& event) {
    wxTreeItemId itemId = event.GetItem();
    if (!itemId.IsOk() || itemId == m_rootId) {
        m_currentDevice = DeviceConfig();
        PopulateForm(m_currentDevice);
        m_connectButton->Enable(false);
        return;
    }

    DeviceTreeItemData* itemData = dynamic_cast<DeviceTreeItemData*>(m_treeCtrl->GetItemData(itemId));
    if (itemData) {
        size_t index = itemData->GetIndex();
        if (index < m_devices.size()) {
            m_currentDevice = m_devices[index];
            PopulateForm(m_currentDevice);
            m_connectButton->Enable(true);
        }
    } else {
        m_connectButton->Enable(false);
    }
}

void ConnectionDialog::OnSave(wxCommandEvent& event) {
    wxString username = m_usernameCtrl->GetValue().Trim(true).Trim(false);
    wxString address = m_addressCtrl->GetValue().Trim(true).Trim(false);
    wxString port = m_portCtrl->GetValue().Trim(true).Trim(false);

    if (port.IsEmpty()) {
        port = "22";
        m_portCtrl->SetValue(port);
    }

    if (username.IsEmpty()) {
        wxMessageBox(TranslationHelper::Tr("loginNameCannotBeEmpty"), TranslationHelper::Tr("error"), wxOK | wxICON_ERROR);
        return;
    }

    if (address.IsEmpty()) {
        wxMessageBox(TranslationHelper::Tr("hostAddressCannotBeEmpty"), TranslationHelper::Tr("error"), wxOK | wxICON_ERROR);
        return;
    }

    m_currentDevice.auth_method = m_authChoice->GetStringSelection() == TranslationHelper::Tr("password") ? "password" : "key";

    if (m_currentDevice.auth_method == "key") {
        wxString keyContent = m_keyTextCtrl->GetValue().Trim(true).Trim(false);
        if (keyContent.IsEmpty()) {
            wxMessageBox(TranslationHelper::Tr("keyContentCannotBeEmpty"), TranslationHelper::Tr("error"), wxOK | wxICON_ERROR);
            return;
        }
    }

    wxString name = m_nameCtrl->GetValue().Trim(true).Trim(false);
    if (name.IsEmpty()) {
        name = username + "@" + address;
        m_nameCtrl->SetValue(name);
    }

    m_currentDevice.name = name.ToStdString();
    m_currentDevice.username = username.ToStdString();
    m_currentDevice.address = address.ToStdString();
    m_currentDevice.port = port.ToStdString();
    m_currentDevice.group = m_groupCtrl->GetValue().ToStdString();

    // Save password or key content based on auth method
    if (m_currentDevice.auth_method == "password") {
        m_currentDevice.password = m_passwordCtrl->GetValue().ToStdString();
    } else {
        wxString keyContent = m_keyTextCtrl->GetValue();
        // Replace Chinese dashes with standard dashes
        keyContent.Replace("——", "-----");
        m_currentDevice.password = keyContent.ToStdString();
    }

    // Find and update by group and name, or add as new
    bool found = false;
    for (auto& dev : m_devices) {
        if (dev.group == m_currentDevice.group && dev.name == m_currentDevice.name) {
            dev = m_currentDevice;
            found = true;
            break;
        }
    }
    if (!found) {
        // Generate a unique ID
        m_currentDevice.id = m_currentDevice.group + "/" + m_currentDevice.name;
        m_devices.push_back(m_currentDevice);
    }

    SaveConfig();
    RebuildTree();

    // Close dialog with save result
    EndModal(wxID_SAVE);
}

void ConnectionDialog::OnConnect(wxCommandEvent& event) {
    // Reload config to ensure password is decrypted with current master password
    LoadConfig();

    // Find the selected device from the reloaded config
    for (const auto& dev : m_devices) {
        if (dev.id == m_currentDevice.id) {
            m_selectedDevice = dev;
            break;
        }
    }

    // Validate key content if using key authentication
    if (m_selectedDevice.auth_method == "key") {
        if (m_selectedDevice.password.empty()) {
            wxMessageBox(TranslationHelper::Tr("keyContentCannotBeEmpty"), TranslationHelper::Tr("error"), wxOK | wxICON_ERROR);
            return;
        }
    }

    EndModal(wxID_OK);
}

void ConnectionDialog::OnCancel(wxCommandEvent& event) {
    EndModal(wxID_CANCEL);
}

void ConnectionDialog::OnAuthMethodChanged(wxCommandEvent& event) {
    UpdatePasswordFieldVisibility();
}

void ConnectionDialog::OnKeyBrowse(wxCommandEvent& event) {
    wxFileDialog openFileDialog(this, TranslationHelper::Tr("selectKeyFile"), "",
                                 "", "Key Files (*.pem;*.key;*.ppk)|*.pem;*.key;*.ppk|All files (*.*)|*.*",
                                 wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (openFileDialog.ShowModal() == wxID_CANCEL) {
        return;
    }

    wxString filePath = openFileDialog.GetPath();
    wxFile file(filePath, wxFile::read);
    if (!file.IsOpened()) {
        wxMessageBox(TranslationHelper::Tr("failedToReadKeyFile"), TranslationHelper::Tr("error"), wxOK | wxICON_ERROR);
        return;
    }

    // Read file content
    size_t fileSize = file.Length();
    char* buffer = new char[fileSize + 1];
    file.Read(buffer, fileSize);
    buffer[fileSize] = '\0';
    file.Close();

    wxString keyContent(buffer, wxConvUTF8);
    delete[] buffer;

    m_keyTextCtrl->SetValue(keyContent);
}

void ConnectionDialog::UpdatePasswordFieldVisibility() {
    bool isKeyAuth = m_authChoice->GetStringSelection() == TranslationHelper::Tr("key");
    m_passwordCtrl->Show(!isKeyAuth);
    m_keyTextCtrl->Show(isKeyAuth);
    m_keyBrowseButton->Show(isKeyAuth);
    Layout();
}

void ConnectionDialog::OnSize(wxSizeEvent& event) {
    event.Skip();
    // Force tree control to recalculate its best size and layout to adapt to new width during dragging
    if (m_treeCtrl) {
        m_treeCtrl->InvalidateBestSize();
        m_treeCtrl->Layout();
        m_treeCtrl->Refresh();
        
#ifdef __WXMSW__
        // On Windows, force the tree control column to expand to fill the available width
        wxSize treeSize = m_treeCtrl->GetSize();
        if (treeSize.GetWidth() > 0) {
            HWND hwndTree = (HWND)m_treeCtrl->GetHandle();
            if (hwndTree) {
                // Send TVM_SETCOLUMNWIDTH message to set the column width (TV_FIRST + 35 = 0x1127)
                ::SendMessage(hwndTree, 0x1127, 0, treeSize.GetWidth() - 20);
            }
        }
#endif
    }
    Layout();
}

