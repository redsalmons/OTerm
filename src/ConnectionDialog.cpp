#include "ConnectionDialog.h"
#include "TranslationHelper.h"
#include <wx/filedlg.h>

ConnectionDialog::ConnectionDialog(wxWindow* parent, const wxString& title)
    : wxDialog(parent, wxID_ANY, title, wxDefaultPosition,
#ifdef __APPLE__
               wxSize(425, 425)
#else
               wxSize(850, 850)
#endif
    ) {

    wxBoxSizer* mainSizer = new wxBoxSizer(wxHORIZONTAL);

    // Left Panel (Tree Control)
#ifdef __APPLE__
    int treeWidth = 125;
#else
    int treeWidth = 250;
#endif
    m_treeCtrl = new wxTreeCtrl(this, wxID_ANY, wxDefaultPosition, wxSize(treeWidth, -1));
    mainSizer->Add(m_treeCtrl, 1, wxEXPAND | wxALL, 5);

    // Right Panel (Form)
    wxPanel* formPanel = new wxPanel(this, wxID_ANY);
    wxBoxSizer* formSizer = new wxBoxSizer(wxVERTICAL);

    wxFlexGridSizer* gridSizer = new wxFlexGridSizer(4, 5, 5);
    gridSizer->Add(new wxStaticText(formPanel, wxID_ANY, TranslationHelper::Tr("deviceName")), 0, wxALIGN_CENTER_VERTICAL);
    m_nameCtrl = new wxTextCtrl(formPanel, wxID_ANY, "");
    gridSizer->Add(m_nameCtrl, 1, wxEXPAND);
    gridSizer->AddSpacer(0);
    gridSizer->AddSpacer(0);

    gridSizer->Add(new wxStaticText(formPanel, wxID_ANY, TranslationHelper::Tr("username")), 0, wxALIGN_CENTER_VERTICAL);
    m_usernameCtrl = new wxTextCtrl(formPanel, wxID_ANY, "");
    gridSizer->Add(m_usernameCtrl, 1, wxEXPAND);
    gridSizer->AddSpacer(0);
    gridSizer->AddSpacer(0);

    // Address and Port on the same line
    gridSizer->Add(new wxStaticText(formPanel, wxID_ANY, TranslationHelper::Tr("hostAddress")), 0, wxALIGN_CENTER_VERTICAL);
    m_addressCtrl = new wxTextCtrl(formPanel, wxID_ANY, "");
    gridSizer->Add(m_addressCtrl, 1, wxEXPAND);
    gridSizer->Add(new wxStaticText(formPanel, wxID_ANY, TranslationHelper::Tr("port")), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 10);
    m_portCtrl = new wxTextCtrl(formPanel, wxID_ANY, "22");
    m_portCtrl->SetMinSize(wxSize(60, -1));
    gridSizer->Add(m_portCtrl, 0);

    gridSizer->Add(new wxStaticText(formPanel, wxID_ANY, TranslationHelper::Tr("deviceGroup")), 0, wxALIGN_CENTER_VERTICAL);
    m_groupCtrl = new wxTextCtrl(formPanel, wxID_ANY, "");
    gridSizer->Add(m_groupCtrl, 1, wxEXPAND);
    gridSizer->AddSpacer(0);
    gridSizer->AddSpacer(0);

    gridSizer->Add(new wxStaticText(formPanel, wxID_ANY, TranslationHelper::Tr("authMethod")), 0, wxALIGN_CENTER_VERTICAL);
    wxString authChoices[] = { TranslationHelper::Tr("password"), TranslationHelper::Tr("key") };
    m_authChoice = new wxChoice(formPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, 2, authChoices);
    m_authChoice->SetSelection(0);
    gridSizer->Add(m_authChoice, 0);
    gridSizer->AddSpacer(0);
    gridSizer->AddSpacer(0);

    gridSizer->Add(new wxStaticText(formPanel, wxID_ANY, TranslationHelper::Tr("passwordKey")), 0, wxALIGN_CENTER_VERTICAL);
    wxBoxSizer* passwordSizer = new wxBoxSizer(wxHORIZONTAL);
    m_passwordCtrl = new wxTextCtrl(formPanel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
    passwordSizer->Add(m_passwordCtrl, 1, wxEXPAND);
    m_keyPathCtrl = new wxTextCtrl(formPanel, wxID_ANY, "");
    m_keyPathCtrl->Hide();
    passwordSizer->Add(m_keyPathCtrl, 1, wxEXPAND);
    m_keyBrowseButton = new wxButton(formPanel, wxID_ANY, TranslationHelper::Tr("browse"));
    m_keyBrowseButton->Hide();
    passwordSizer->Add(m_keyBrowseButton, 0, wxLEFT, 5);
    gridSizer->Add(passwordSizer, 1, wxEXPAND);
    gridSizer->AddSpacer(0);
    gridSizer->AddSpacer(0);

    gridSizer->AddGrowableCol(1, 1);
    formSizer->Add(gridSizer, 1, wxEXPAND | wxALL, 10);

    wxBoxSizer* buttonSizer = new wxBoxSizer(wxHORIZONTAL);
    buttonSizer->Add(new wxButton(formPanel, wxID_SAVE, TranslationHelper::Tr("save")), 0, wxALL, 5);
    m_connectButton = new wxButton(formPanel, wxID_OK, TranslationHelper::Tr("connect"));
    m_connectButton->Enable(false);
    buttonSizer->Add(m_connectButton, 0, wxALL, 5);
    buttonSizer->Add(new wxButton(formPanel, wxID_CANCEL, TranslationHelper::Tr("cancel")), 0, wxALL, 5);
    formSizer->Add(buttonSizer, 0, wxALIGN_RIGHT | wxALL, 10);

    formPanel->SetSizer(formSizer);
    mainSizer->Add(formPanel, 2, wxEXPAND | wxALL, 5);

    SetSizerAndFit(mainSizer);
#ifdef __APPLE__
    SetSize(wxSize(425, 425));
#else
    SetSize(wxSize(850, 850));
#endif
    Centre();

    LoadConfig();
    RebuildTree();

    Bind(wxEVT_TREE_SEL_CHANGED, &ConnectionDialog::OnTreeSelectionChanged, this, m_treeCtrl->GetId());
    Bind(wxEVT_BUTTON, &ConnectionDialog::OnSave, this, wxID_SAVE);
    Bind(wxEVT_BUTTON, &ConnectionDialog::OnConnect, this, wxID_OK);
    Bind(wxEVT_BUTTON, &ConnectionDialog::OnCancel, this, wxID_CANCEL);
    Bind(wxEVT_CHOICE, &ConnectionDialog::OnAuthMethodChanged, this, m_authChoice->GetId());
    Bind(wxEVT_BUTTON, &ConnectionDialog::OnKeyBrowse, this, m_keyBrowseButton->GetId());
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
    m_keyPathCtrl->SetValue(device.password);
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
    m_currentDevice.name = m_nameCtrl->GetValue().ToStdString();
    m_currentDevice.username = m_usernameCtrl->GetValue().ToStdString();
    m_currentDevice.address = m_addressCtrl->GetValue().ToStdString();
    m_currentDevice.port = m_portCtrl->GetValue().ToStdString();
    if (m_currentDevice.port.empty()) {
        m_currentDevice.port = "22";
    }
    m_currentDevice.group = m_groupCtrl->GetValue().ToStdString();
    m_currentDevice.auth_method = m_authChoice->GetStringSelection() == TranslationHelper::Tr("password") ? "password" : "key";

    // Save password or key path based on auth method
    if (m_currentDevice.auth_method == "password") {
        m_currentDevice.password = m_passwordCtrl->GetValue().ToStdString();
    } else {
        m_currentDevice.password = m_keyPathCtrl->GetValue().ToStdString();
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
}

void ConnectionDialog::OnConnect(wxCommandEvent& event) {
    m_selectedDevice = m_currentDevice;
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
                                 "", "Key Files (*.pem;*.key)|*.pem;*.key|All files (*.*)|*.*",
                                 wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (openFileDialog.ShowModal() == wxID_CANCEL) {
        return;
    }
    m_keyPathCtrl->SetValue(openFileDialog.GetPath());
}

void ConnectionDialog::UpdatePasswordFieldVisibility() {
    bool isKeyAuth = m_authChoice->GetStringSelection() == TranslationHelper::Tr("key");
    m_passwordCtrl->Show(!isKeyAuth);
    m_keyPathCtrl->Show(isKeyAuth);
    m_keyBrowseButton->Show(isKeyAuth);
    Layout();
}

