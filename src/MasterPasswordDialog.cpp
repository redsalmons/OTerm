#include "MasterPasswordDialog.h"
#include "TranslationHelper.h"
#include <wx/sizer.h>
#include <wx/stattext.h>

wxBEGIN_EVENT_TABLE(MasterPasswordDialog, wxDialog)
    EVT_BUTTON(wxID_OK, MasterPasswordDialog::OnOK)
    EVT_BUTTON(wxID_CANCEL, MasterPasswordDialog::OnCancel)
    EVT_TEXT_ENTER(wxID_ANY, MasterPasswordDialog::OnOK)
wxEND_EVENT_TABLE()

MasterPasswordDialog::MasterPasswordDialog(wxWindow* parent, bool isNewPassword)
    : wxDialog(parent, wxID_ANY, isNewPassword ? TranslationHelper::Tr("setupMasterPassword") : TranslationHelper::Tr("enterMasterPassword"),
                wxDefaultPosition, wxDefaultSize),
      m_isNewPassword(isNewPassword),
      m_passwordVerifier(nullptr) {

    wxPanel* panel = new wxPanel(this);
    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

    if (isNewPassword) {
        // New password setup
        wxBoxSizer* password1Sizer = new wxBoxSizer(wxHORIZONTAL);
        password1Sizer->Add(new wxStaticText(panel, wxID_ANY, TranslationHelper::Tr("enterPassword")), 0, wxALIGN_CENTER_VERTICAL | wxALL, 10);
        m_passwordCtrl = new wxTextCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD | wxTE_PROCESS_ENTER);
        password1Sizer->Add(m_passwordCtrl, 1, wxEXPAND | wxALL, 10);
        mainSizer->Add(password1Sizer, 0, wxEXPAND);

        wxBoxSizer* password2Sizer = new wxBoxSizer(wxHORIZONTAL);
        password2Sizer->Add(new wxStaticText(panel, wxID_ANY, TranslationHelper::Tr("confirmPassword")), 0, wxALIGN_CENTER_VERTICAL | wxALL, 10);
        m_confirmPasswordCtrl = new wxTextCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD | wxTE_PROCESS_ENTER);
        password2Sizer->Add(m_confirmPasswordCtrl, 1, wxEXPAND | wxALL, 10);
        mainSizer->Add(password2Sizer, 0, wxEXPAND);
    } else {
        // Existing password login
        wxBoxSizer* passwordSizer = new wxBoxSizer(wxHORIZONTAL);
        passwordSizer->Add(new wxStaticText(panel, wxID_ANY, TranslationHelper::Tr("enterPassword")), 0, wxALIGN_CENTER_VERTICAL | wxALL, 10);
        m_passwordCtrl = new wxTextCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD | wxTE_PROCESS_ENTER);
        passwordSizer->Add(m_passwordCtrl, 1, wxEXPAND | wxALL, 10);
        mainSizer->Add(passwordSizer, 0, wxEXPAND);
        m_confirmPasswordCtrl = nullptr;
    }

    // Warning text for new password setup
    if (isNewPassword) {
        m_warningText = new wxTextCtrl(panel, wxID_ANY, TranslationHelper::Tr("masterPasswordWarning"),
                                     wxDefaultPosition, wxSize(-1, 120),
                                     wxTE_MULTILINE | wxTE_READONLY | wxTE_NO_VSCROLL | wxBORDER_NONE);
        m_warningText->SetBackgroundColour(panel->GetBackgroundColour());
        m_warningText->SetForegroundColour(wxColour(255, 0, 0));
        mainSizer->Add(m_warningText, 0, wxEXPAND | wxALL, 10);
    } else {
        m_warningText = nullptr;
    }

    // Buttons
    wxBoxSizer* buttonSizer = new wxBoxSizer(wxHORIZONTAL);
    buttonSizer->Add(new wxButton(panel, wxID_OK, TranslationHelper::Tr("ok")), 0, wxALL, 5);
    buttonSizer->Add(new wxButton(panel, wxID_CANCEL, TranslationHelper::Tr("cancel")), 0, wxALL, 5);
    mainSizer->Add(buttonSizer, 0, wxALIGN_RIGHT | wxALL, 10);

    // Error label (hidden by default, below buttons)
    m_errorLabel = new wxStaticText(panel, wxID_ANY, "");
    m_errorLabel->SetForegroundColour(wxColour(255, 0, 0));
    m_errorLabel->Hide();
    mainSizer->Add(m_errorLabel, 0, wxALL, 10);

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

void MasterPasswordDialog::SetError(const wxString& error) {
    if (error.IsEmpty()) {
        m_errorLabel->Hide();
    } else {
        m_errorLabel->SetLabel(error);
        m_errorLabel->Show();
    }
    Layout();
    Fit();
}

void MasterPasswordDialog::SetPasswordVerifier(std::function<bool(const wxString&, wxString&, bool&)> verifier) {
    m_passwordVerifier = verifier;
}

void MasterPasswordDialog::OnOK(wxCommandEvent& event) {
    wxString password = m_passwordCtrl->GetValue();

    if (password.IsEmpty()) {
        SetError(TranslationHelper::Tr("passwordCannotBeEmpty"));
        return;
    }

    if (m_isNewPassword) {
        wxString confirmPassword = m_confirmPasswordCtrl->GetValue();
        if (password != confirmPassword) {
            SetError(TranslationHelper::Tr("passwordsDoNotMatch"));
            return;
        }
    }

    // Use password verifier if set
    if (m_passwordVerifier) {
        wxString errorMsg;
        bool shouldClose = false;
        if (!m_passwordVerifier(password, errorMsg, shouldClose)) {
            SetError(errorMsg);
            if (shouldClose) {
                EndModal(wxID_CANCEL);
            }
            return;  // Don't close the dialog, just show error
        }
    }

    EndModal(wxID_OK);
}

void MasterPasswordDialog::OnCancel(wxCommandEvent& event) {
    EndModal(wxID_CANCEL);
}
