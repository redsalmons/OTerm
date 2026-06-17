#include "AddDeviceDialog.h"
#include "TranslationHelper.h"
#include "GlobalConfig.h"
#include <wx/filedlg.h>
#include <wx/textfile.h>

AddDeviceDialog::AddDeviceDialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, TranslationHelper::Tr("addDevice"), wxDefaultPosition, wxSize(500, 450)) {
    
    SetBackgroundColour(wxColour(10, 10, 10));
    
    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
    
    // Form grid
    wxFlexGridSizer* gridSizer = new wxFlexGridSizer(2, 10, 10);
    gridSizer->AddGrowableCol(1);
    
    // Device Name
    wxStaticText* nameLabel = new wxStaticText(this, wxID_ANY, TranslationHelper::Tr("deviceName"));
    nameLabel->SetForegroundColour(wxColour(200, 200, 200));
    m_nameCtrl = new wxTextCtrl(this, wxID_ANY);
    m_nameCtrl->SetBackgroundColour(wxColour(30, 30, 30));
    m_nameCtrl->SetForegroundColour(wxColour(255, 255, 255));
    gridSizer->Add(nameLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
    gridSizer->Add(m_nameCtrl, 1, wxEXPAND);
    
    // Username
    wxStaticText* usernameLabel = new wxStaticText(this, wxID_ANY, TranslationHelper::Tr("username"));
    usernameLabel->SetForegroundColour(wxColour(200, 200, 200));
    m_usernameCtrl = new wxTextCtrl(this, wxID_ANY);
    m_usernameCtrl->SetBackgroundColour(wxColour(30, 30, 30));
    m_usernameCtrl->SetForegroundColour(wxColour(255, 255, 255));
    gridSizer->Add(usernameLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
    gridSizer->Add(m_usernameCtrl, 1, wxEXPAND);
    
    // Address
    wxStaticText* addressLabel = new wxStaticText(this, wxID_ANY, TranslationHelper::Tr("hostAddress"));
    addressLabel->SetForegroundColour(wxColour(200, 200, 200));
    m_addressCtrl = new wxTextCtrl(this, wxID_ANY);
    m_addressCtrl->SetBackgroundColour(wxColour(30, 30, 30));
    m_addressCtrl->SetForegroundColour(wxColour(255, 255, 255));
    gridSizer->Add(addressLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
    gridSizer->Add(m_addressCtrl, 1, wxEXPAND);
    
    // Port
    wxStaticText* portLabel = new wxStaticText(this, wxID_ANY, TranslationHelper::Tr("port"));
    portLabel->SetForegroundColour(wxColour(200, 200, 200));
    m_portCtrl = new wxTextCtrl(this, wxID_ANY, "22");
    m_portCtrl->SetBackgroundColour(wxColour(30, 30, 30));
    m_portCtrl->SetForegroundColour(wxColour(255, 255, 255));
    gridSizer->Add(portLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
    gridSizer->Add(m_portCtrl, 1, wxEXPAND);
    
    // Group
    wxStaticText* groupLabel = new wxStaticText(this, wxID_ANY, TranslationHelper::Tr("deviceGroup"));
    groupLabel->SetForegroundColour(wxColour(200, 200, 200));
    m_groupCtrl = new wxTextCtrl(this, wxID_ANY);
    m_groupCtrl->SetBackgroundColour(wxColour(30, 30, 30));
    m_groupCtrl->SetForegroundColour(wxColour(255, 255, 255));
    gridSizer->Add(groupLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
    gridSizer->Add(m_groupCtrl, 1, wxEXPAND);
    
    // Auth Method
    wxStaticText* authLabel = new wxStaticText(this, wxID_ANY, TranslationHelper::Tr("authMethod"));
    authLabel->SetForegroundColour(wxColour(200, 200, 200));
    m_authChoice = new wxChoice(this, wxID_ANY);
    m_authChoice->Append(TranslationHelper::Tr("password"));
    m_authChoice->Append(TranslationHelper::Tr("key"));
    m_authChoice->SetSelection(0);
    m_authChoice->SetBackgroundColour(wxColour(30, 30, 30));
    m_authChoice->SetForegroundColour(wxColour(255, 255, 255));
    gridSizer->Add(authLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
    gridSizer->Add(m_authChoice, 1, wxEXPAND);
    
    // Password/Key
    wxStaticText* passwordLabel = new wxStaticText(this, wxID_ANY, TranslationHelper::Tr("passwordKey"));
    passwordLabel->SetForegroundColour(wxColour(200, 200, 200));
    
    wxBoxSizer* passwordSizer = new wxBoxSizer(wxHORIZONTAL);
    m_passwordCtrl = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
    m_passwordCtrl->SetBackgroundColour(wxColour(30, 30, 30));
    m_passwordCtrl->SetForegroundColour(wxColour(255, 255, 255));
    
    m_keyTextCtrl = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_DONTWRAP);
    m_keyTextCtrl->SetBackgroundColour(wxColour(30, 30, 30));
    m_keyTextCtrl->SetForegroundColour(wxColour(255, 255, 255));
    m_keyTextCtrl->Hide();
    
    m_keyBrowseButton = new wxButton(this, wxID_ANY, TranslationHelper::Tr("browse"));
    m_keyBrowseButton->Hide();
    
    passwordSizer->Add(m_passwordCtrl, 1, wxEXPAND);
    passwordSizer->Add(m_keyTextCtrl, 1, wxEXPAND);
    passwordSizer->Add(m_keyBrowseButton, 0, wxLEFT, 5);
    
    gridSizer->Add(passwordLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
    gridSizer->Add(passwordSizer, 1, wxEXPAND);
    
    mainSizer->Add(gridSizer, 1, wxALL | wxEXPAND, 20);
    
    // Buttons
    wxBoxSizer* buttonSizer = new wxBoxSizer(wxHORIZONTAL);
    buttonSizer->AddStretchSpacer();
    
    m_saveButton = new wxButton(this, wxID_SAVE, TranslationHelper::Tr("save"));
    m_saveButton->SetBackgroundColour(wxColour(60, 100, 180));
    m_saveButton->SetForegroundColour(wxColour(255, 255, 255));
    
    wxButton* cancelButton = new wxButton(this, wxID_CANCEL, TranslationHelper::Tr("cancel"));
    cancelButton->SetBackgroundColour(wxColour(100, 100, 100));
    cancelButton->SetForegroundColour(wxColour(255, 255, 255));
    
    buttonSizer->Add(m_saveButton, 0, wxRIGHT, 10);
    buttonSizer->Add(cancelButton, 0);
    
    mainSizer->Add(buttonSizer, 0, wxALL | wxALIGN_RIGHT, 20);
    
    SetSizer(mainSizer);
    
    // Bind events
    Bind(wxEVT_BUTTON, &AddDeviceDialog::OnSave, this, wxID_SAVE);
    Bind(wxEVT_BUTTON, &AddDeviceDialog::OnCancel, this, wxID_CANCEL);
    m_authChoice->Bind(wxEVT_CHOICE, &AddDeviceDialog::OnAuthMethodChanged, this);
    m_keyBrowseButton->Bind(wxEVT_BUTTON, &AddDeviceDialog::OnKeyBrowse, this);
    
    UpdatePasswordFieldVisibility();
}

void AddDeviceDialog::OnSave(wxCommandEvent& event) {
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

    int authSelection = m_authChoice->GetSelection();
    if (authSelection == 1) {
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

    m_deviceConfig.name = name.ToStdString();
    m_deviceConfig.username = username.ToStdString();
    m_deviceConfig.address = address.ToStdString();
    m_deviceConfig.port = port.ToStdString();
    m_deviceConfig.group = m_groupCtrl->GetValue().ToStdString();
    
    if (authSelection == 0) {
        m_deviceConfig.auth_method = "password";
        m_deviceConfig.password = m_passwordCtrl->GetValue().ToStdString();
    } else {
        m_deviceConfig.auth_method = "key";
        m_deviceConfig.key = m_keyTextCtrl->GetValue().ToStdString();
    }
    
    // Generate ID
    m_deviceConfig.id = std::to_string(std::hash<std::string>{}(m_deviceConfig.name + m_deviceConfig.address));
    
    EndModal(wxID_OK);
}

void AddDeviceDialog::OnCancel(wxCommandEvent& event) {
    EndModal(wxID_CANCEL);
}

void AddDeviceDialog::OnAuthMethodChanged(wxCommandEvent& event) {
    UpdatePasswordFieldVisibility();
}

void AddDeviceDialog::OnKeyBrowse(wxCommandEvent& event) {
    wxFileDialog openFileDialog(this, TranslationHelper::Tr("selectKeyFile"), "", "",
                               "Key files (*.pem, *.key)|*.pem;*.key|All files (*.*)|*.*",
                               wxFD_OPEN | wxFD_FILE_MUST_EXIST);

    if (openFileDialog.ShowModal() == wxID_OK) {
        wxString path = openFileDialog.GetPath();
        wxTextFile file(path);
        if (file.Exists() && file.Open()) {
            wxString content;
            for (size_t i = 0; i < file.GetLineCount(); i++) {
                content += file.GetLine(i) + "\n";
            }
            m_keyTextCtrl->SetValue(content);
            file.Close();
        }
    }
}

void AddDeviceDialog::UpdatePasswordFieldVisibility() {
    int selection = m_authChoice->GetSelection();
    if (selection == 0) {
        // Password
        m_passwordCtrl->Show();
        m_keyTextCtrl->Hide();
        m_keyBrowseButton->Hide();
    } else {
        // Key
        m_passwordCtrl->Hide();
        m_keyTextCtrl->Show();
        m_keyBrowseButton->Show();
    }
    Layout();
}
