#include "SettingsDialog.h"
#include "MonospaceFontDialog.h"
#include "TranslationHelper.h"
#include "GlobalConfig.h"
#include "DeviceConfig.h"
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/statbox.h>
#include <wx/notebook.h>

wxBEGIN_EVENT_TABLE(SettingsDialog, wxDialog)
    EVT_BUTTON(wxID_OK, SettingsDialog::OnOK)
    EVT_BUTTON(wxID_CANCEL, SettingsDialog::OnCancel)
wxEND_EVENT_TABLE()

SettingsDialog::SettingsDialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, TranslationHelper::Tr("settings"), wxDefaultPosition, wxSize(450, 350)) {

    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

    // Create Tab control (Notebook)
    wxNotebook* notebook = new wxNotebook(this, wxID_ANY);

    // -------------------------------------------------------------
    // Tab 1: Basic configuration
    // -------------------------------------------------------------
    wxPanel* basicPanel = new wxPanel(notebook, wxID_ANY);
    wxBoxSizer* basicSizer = new wxBoxSizer(wxVERTICAL);

    // Language selection
    wxBoxSizer* languageSizer = new wxBoxSizer(wxHORIZONTAL);
    languageSizer->Add(new wxStaticText(basicPanel, wxID_ANY, TranslationHelper::Tr("language")), 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);

    wxString languageChoices[] = { TranslationHelper::Tr("chinese"), TranslationHelper::Tr("english") };
    m_languageChoice = new wxChoice(basicPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, 2, languageChoices);

    // Set current language
    std::string currentLang = GlobalConfig::GetLanguage();
    if (currentLang == "zh_CN") {
        m_languageChoice->SetSelection(0);
    } else {
        m_languageChoice->SetSelection(1);
    }

    languageSizer->Add(m_languageChoice, 1, wxEXPAND | wxALL, 5);
    basicSizer->Add(languageSizer, 0, wxEXPAND | wxALL, 10);

    // Font selection
    wxBoxSizer* fontSizer = new wxBoxSizer(wxHORIZONTAL);
    fontSizer->Add(new wxStaticText(basicPanel, wxID_ANY, TranslationHelper::Tr("font")), 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);

    std::string currentFontName = GlobalConfig::GetFontName();
    int currentFontSize = GlobalConfig::GetFontSize();

    m_fontButton = new wxButton(basicPanel, wxID_ANY, "Select Font");
    fontSizer->Add(m_fontButton, 0, wxALL, 5);

    m_fontDisplayText = new wxStaticText(basicPanel, wxID_ANY, wxString::FromUTF8(currentFontName.c_str()));
    fontSizer->Add(m_fontDisplayText, 1, wxEXPAND | wxALL, 5);
    basicSizer->Add(fontSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

    // Bind font button event
    m_fontButton->Bind(wxEVT_BUTTON, &SettingsDialog::OnFontButtonClicked, this);

    // Font size
    wxBoxSizer* fontSizeSizer = new wxBoxSizer(wxHORIZONTAL);
    fontSizeSizer->Add(new wxStaticText(basicPanel, wxID_ANY, TranslationHelper::Tr("fontSize")), 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);

    m_fontSizeSpin = new wxSpinCtrl(basicPanel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 8, 72, currentFontSize);
    fontSizeSizer->Add(m_fontSizeSpin, 0, wxALL, 5);
    basicSizer->Add(fontSizeSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

    basicPanel->SetSizer(basicSizer);
    notebook->AddPage(basicPanel, TranslationHelper::Tr("basicConfig"));

    // -------------------------------------------------------------
    // Tab 2: Master password group
    // -------------------------------------------------------------
    wxPanel* passwordPanel = new wxPanel(notebook, wxID_ANY);
    wxBoxSizer* passwordSizer = new wxBoxSizer(wxVERTICAL);

    // Show password input fields
    wxBoxSizer* password1Sizer = new wxBoxSizer(wxHORIZONTAL);
    password1Sizer->Add(new wxStaticText(passwordPanel, wxID_ANY, TranslationHelper::Tr("enterPassword")), 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);
    m_masterPassword1 = new wxTextCtrl(passwordPanel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
    password1Sizer->Add(m_masterPassword1, 1, wxEXPAND | wxALL, 5);
    passwordSizer->Add(password1Sizer, 0, wxEXPAND | wxALL, 10);

    wxBoxSizer* password2Sizer = new wxBoxSizer(wxHORIZONTAL);
    password2Sizer->Add(new wxStaticText(passwordPanel, wxID_ANY, TranslationHelper::Tr("confirmPassword")), 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);
    m_masterPassword2 = new wxTextCtrl(passwordPanel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
    password2Sizer->Add(m_masterPassword2, 1, wxEXPAND | wxALL, 5);
    passwordSizer->Add(password2Sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

    // Show reset button
    wxBoxSizer* masterButtonSizer = new wxBoxSizer(wxHORIZONTAL);
    wxButton* resetButton = new wxButton(passwordPanel, wxID_ANY, TranslationHelper::Tr("reset"));
    masterButtonSizer->Add(resetButton, 0, wxALL, 5);
    passwordSizer->Add(masterButtonSizer, 0, wxALIGN_RIGHT | wxRIGHT | wxBOTTOM, 10);

    // Bind button event
    resetButton->Bind(wxEVT_BUTTON, &SettingsDialog::OnResetMasterPassword, this);

    passwordPanel->SetSizer(passwordSizer);
    notebook->AddPage(passwordPanel, TranslationHelper::Tr("masterPasswordConfig"));

    // Add notebook to dialog sizer
    mainSizer->Add(notebook, 1, wxEXPAND | wxALL, 10);

    // Bottom standard buttons
    wxBoxSizer* bottomButtonSizer = new wxBoxSizer(wxHORIZONTAL);
    bottomButtonSizer->Add(new wxButton(this, wxID_OK, TranslationHelper::Tr("save")), 0, wxALL, 5);
    bottomButtonSizer->Add(new wxButton(this, wxID_CANCEL, TranslationHelper::Tr("cancel")), 0, wxALL, 5);
    mainSizer->Add(bottomButtonSizer, 0, wxALIGN_RIGHT | wxLEFT | wxRIGHT | wxBOTTOM, 10);

    SetSizerAndFit(mainSizer);
    Centre();
}

SettingsDialog::~SettingsDialog() {
}

void SettingsDialog::OnOK(wxCommandEvent& event) {
    int selection = m_languageChoice->GetSelection();
    std::string newLang = (selection == 0) ? "zh_CN" : "en";

    // Save language to config
    GlobalConfig::SetLanguage(newLang);

    // Save font settings
    GlobalConfig::SetFontName(m_fontDisplayText->GetLabel().ToStdString());
    GlobalConfig::SetFontSize(m_fontSizeSpin->GetValue());

    GlobalConfig::SaveSettings();

    // Reload translations
    TranslationHelper::Load(wxString::FromUTF8(newLang.c_str()));

    EndModal(wxID_OK);
}

void SettingsDialog::OnCancel(wxCommandEvent& event) {
    EndModal(wxID_CANCEL);
}

void SettingsDialog::OnFontButtonClicked(wxCommandEvent& event) {
    std::string currentFontName = GlobalConfig::GetFontName();
    MonospaceFontDialog fontDialog(this, wxString::FromUTF8(currentFontName.c_str()));

    if (fontDialog.ShowModal() == wxID_OK) {
        wxString selectedFont = fontDialog.GetSelectedFont();
        if (!selectedFont.IsEmpty()) {
            m_fontDisplayText->SetLabel(selectedFont);
        }
    }
}

void SettingsDialog::OnResetMasterPassword(wxCommandEvent& event) {
    wxString password1 = m_masterPassword1->GetValue();
    wxString password2 = m_masterPassword2->GetValue();

    // 1. Check if passwords match
    if (password1.IsEmpty()) {
        wxMessageBox(TranslationHelper::Tr("passwordCannotBeEmpty"), TranslationHelper::Tr("error"), wxOK | wxICON_ERROR);
        return;
    }

    if (password1 != password2) {
        wxMessageBox(TranslationHelper::Tr("passwordsDoNotMatch"), TranslationHelper::Tr("error"), wxOK | wxICON_ERROR);
        return;
    }

    // 2.1-2.3: Re-encrypt oc.json with new password
    std::string oldPassword = GlobalConfig::GetActiveMasterPassword();
    std::string newPassword = password1.ToStdString();

    if (!DeviceConfig::ReencryptWithNewPassword(oldPassword, newPassword)) {
        wxMessageBox(TranslationHelper::Tr("reencryptFailed"), TranslationHelper::Tr("error"), wxOK | wxICON_ERROR);
        return;
    }

    // 2.4: Update global master password
    GlobalConfig::SetMasterPassword(newPassword);
    GlobalConfig::SaveSettings();
    GlobalConfig::SetActiveMasterPassword(newPassword);
    wxMessageBox(TranslationHelper::Tr("masterPasswordResetSuccessfully"), TranslationHelper::Tr("success"), wxOK | wxICON_INFORMATION);
}
