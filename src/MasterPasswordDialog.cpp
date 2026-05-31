#include "MasterPasswordDialog.h"
#include "TranslationHelper.h"
#include <wx/sizer.h>
#include <wx/stattext.h>

wxBEGIN_EVENT_TABLE(MasterPasswordDialog, wxDialog)
    EVT_BUTTON(wxID_OK, MasterPasswordDialog::OnOK)
    EVT_BUTTON(wxID_CANCEL, MasterPasswordDialog::OnCancel)
wxEND_EVENT_TABLE()

MasterPasswordDialog::MasterPasswordDialog(wxWindow* parent, bool isNewPassword)
    : wxDialog(parent, wxID_ANY, isNewPassword ? TranslationHelper::Tr("setupMasterPassword") : TranslationHelper::Tr("enterMasterPassword"),
                wxDefaultPosition, wxSize(400, isNewPassword ? 200 : 150)),
      m_isNewPassword(isNewPassword) {

    wxPanel* panel = new wxPanel(this);
    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

    if (isNewPassword) {
        // New password setup
        wxBoxSizer* password1Sizer = new wxBoxSizer(wxHORIZONTAL);
        password1Sizer->Add(new wxStaticText(panel, wxID_ANY, TranslationHelper::Tr("enterPassword")), 0, wxALIGN_CENTER_VERTICAL | wxALL, 10);
        m_passwordCtrl = new wxTextCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
        password1Sizer->Add(m_passwordCtrl, 1, wxEXPAND | wxALL, 10);
        mainSizer->Add(password1Sizer, 0, wxEXPAND);

        wxBoxSizer* password2Sizer = new wxBoxSizer(wxHORIZONTAL);
        password2Sizer->Add(new wxStaticText(panel, wxID_ANY, TranslationHelper::Tr("confirmPassword")), 0, wxALIGN_CENTER_VERTICAL | wxALL, 10);
        m_confirmPasswordCtrl = new wxTextCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
        password2Sizer->Add(m_confirmPasswordCtrl, 1, wxEXPAND | wxALL, 10);
        mainSizer->Add(password2Sizer, 0, wxEXPAND);
    } else {
        // Existing password login
        wxBoxSizer* passwordSizer = new wxBoxSizer(wxHORIZONTAL);
        passwordSizer->Add(new wxStaticText(panel, wxID_ANY, TranslationHelper::Tr("enterPassword")), 0, wxALIGN_CENTER_VERTICAL | wxALL, 10);
        m_passwordCtrl = new wxTextCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
        passwordSizer->Add(m_passwordCtrl, 1, wxEXPAND | wxALL, 10);
        mainSizer->Add(passwordSizer, 0, wxEXPAND);
        m_confirmPasswordCtrl = nullptr;
    }

    // Buttons
    wxBoxSizer* buttonSizer = new wxBoxSizer(wxHORIZONTAL);
    buttonSizer->Add(new wxButton(panel, wxID_OK, TranslationHelper::Tr("ok")), 0, wxALL, 5);
    buttonSizer->Add(new wxButton(panel, wxID_CANCEL, TranslationHelper::Tr("cancel")), 0, wxALL, 5);
    mainSizer->Add(buttonSizer, 0, wxALIGN_RIGHT | wxALL, 10);

    panel->SetSizer(mainSizer);
    wxBoxSizer* dialogSizer = new wxBoxSizer(wxVERTICAL);
    dialogSizer->Add(panel, 1, wxEXPAND);
    SetSizerAndFit(dialogSizer);
    Centre();

    if (m_passwordCtrl) {
        m_passwordCtrl->SetFocus();
    }
}

MasterPasswordDialog::~MasterPasswordDialog() {
}

wxString MasterPasswordDialog::GetPassword() const {
    return m_passwordCtrl->GetValue();
}

void MasterPasswordDialog::OnOK(wxCommandEvent& event) {
    wxString password = m_passwordCtrl->GetValue();

    if (password.IsEmpty()) {
        wxMessageBox(TranslationHelper::Tr("passwordCannotBeEmpty"), TranslationHelper::Tr("error"), wxOK | wxICON_ERROR);
        return;
    }

    if (m_isNewPassword) {
        wxString confirmPassword = m_confirmPasswordCtrl->GetValue();
        if (password != confirmPassword) {
            wxMessageBox(TranslationHelper::Tr("passwordsDoNotMatch"), TranslationHelper::Tr("error"), wxOK | wxICON_ERROR);
            return;
        }
    }

    EndModal(wxID_OK);
}

void MasterPasswordDialog::OnCancel(wxCommandEvent& event) {
    EndModal(wxID_CANCEL);
}
