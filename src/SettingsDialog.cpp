#include "SettingsDialog.h"
#include "TranslationHelper.h"
#include "GlobalConfig.h"
#include "DeviceConfig.h"
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/statbox.h>

wxBEGIN_EVENT_TABLE(SettingsDialog, wxDialog)
    EVT_BUTTON(wxID_OK, SettingsDialog::OnOK)
    EVT_BUTTON(wxID_CANCEL, SettingsDialog::OnCancel)
wxEND_EVENT_TABLE()

SettingsDialog::SettingsDialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, TranslationHelper::Tr("settings"), wxDefaultPosition, wxSize(450, 400)) {

    wxPanel* panel = new wxPanel(this);
    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

    // Basic configuration group
    wxStaticBox* basicConfigBox = new wxStaticBox(panel, wxID_ANY, TranslationHelper::Tr("basicConfig"));
    wxStaticBoxSizer* basicConfigSizer = new wxStaticBoxSizer(basicConfigBox, wxVERTICAL);

    // Language selection
    wxBoxSizer* languageSizer = new wxBoxSizer(wxHORIZONTAL);
    languageSizer->Add(new wxStaticText(panel, wxID_ANY, TranslationHelper::Tr("language")), 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);

    wxString languageChoices[] = { TranslationHelper::Tr("chinese"), TranslationHelper::Tr("english") };
    m_languageChoice = new wxChoice(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, 2, languageChoices);

    // Set current language
    std::string currentLang = GlobalConfig::GetLanguage();
    if (currentLang == "zh_CN") {
        m_languageChoice->SetSelection(0);
    } else {
        m_languageChoice->SetSelection(1);
    }

    languageSizer->Add(m_languageChoice, 1, wxEXPAND | wxALL, 5);
    basicConfigSizer->Add(languageSizer, 0, wxEXPAND);

    // Font selection
    wxBoxSizer* fontSizer = new wxBoxSizer(wxHORIZONTAL);
    fontSizer->Add(new wxStaticText(panel, wxID_ANY, TranslationHelper::Tr("font")), 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);

    // Set current font
    std::string currentFontName = GlobalConfig::GetFontName();
    int currentFontSize = GlobalConfig::GetFontSize();
    wxFont currentFont(currentFontSize, wxFONTFAMILY_MODERN, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxString::FromUTF8(currentFontName.c_str()));

    m_fontPicker = new wxFontPickerCtrl(panel, wxID_ANY, currentFont);
    fontSizer->Add(m_fontPicker, 1, wxEXPAND | wxALL, 5);
    basicConfigSizer->Add(fontSizer, 0, wxEXPAND);

    // Font size
    wxBoxSizer* fontSizeSizer = new wxBoxSizer(wxHORIZONTAL);
    fontSizeSizer->Add(new wxStaticText(panel, wxID_ANY, TranslationHelper::Tr("fontSize")), 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);

    m_fontSizeSpin = new wxSpinCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 8, 72, currentFontSize);
    fontSizeSizer->Add(m_fontSizeSpin, 0, wxALL, 5);
    basicConfigSizer->Add(fontSizeSizer, 0, wxEXPAND);

    // Save and Cancel buttons in basic config group
    wxBoxSizer* basicButtonSizer = new wxBoxSizer(wxHORIZONTAL);
    basicButtonSizer->Add(new wxButton(panel, wxID_OK, TranslationHelper::Tr("save")), 0, wxALL, 5);
    basicButtonSizer->Add(new wxButton(panel, wxID_CANCEL, TranslationHelper::Tr("cancel")), 0, wxALL, 5);
    basicConfigSizer->Add(basicButtonSizer, 0, wxALIGN_RIGHT | wxALL, 5);

    mainSizer->Add(basicConfigSizer, 0, wxEXPAND | wxALL, 10);

    // Master password group
    wxStaticBox* masterPasswordBox = new wxStaticBox(panel, wxID_ANY, TranslationHelper::Tr("masterPasswordConfig"));
    wxStaticBoxSizer* masterPasswordSizer = new wxStaticBoxSizer(masterPasswordBox, wxVERTICAL);

    // Show password input fields
    wxBoxSizer* password1Sizer = new wxBoxSizer(wxHORIZONTAL);
    password1Sizer->Add(new wxStaticText(panel, wxID_ANY, TranslationHelper::Tr("enterPassword")), 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);
    m_masterPassword1 = new wxTextCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
    password1Sizer->Add(m_masterPassword1, 1, wxEXPAND | wxALL, 5);
    masterPasswordSizer->Add(password1Sizer, 0, wxEXPAND);

    wxBoxSizer* password2Sizer = new wxBoxSizer(wxHORIZONTAL);
    password2Sizer->Add(new wxStaticText(panel, wxID_ANY, TranslationHelper::Tr("confirmPassword")), 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);
    m_masterPassword2 = new wxTextCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
    password2Sizer->Add(m_masterPassword2, 1, wxEXPAND | wxALL, 5);
    masterPasswordSizer->Add(password2Sizer, 0, wxEXPAND);

    // Only show reset button
    wxBoxSizer* masterButtonSizer = new wxBoxSizer(wxHORIZONTAL);
    wxButton* resetButton = new wxButton(panel, wxID_ANY, TranslationHelper::Tr("reset"));
    masterButtonSizer->Add(resetButton, 0, wxALL, 5);
    masterPasswordSizer->Add(masterButtonSizer, 0, wxALIGN_RIGHT | wxALL, 5);

    // Bind button event
    resetButton->Bind(wxEVT_BUTTON, &SettingsDialog::OnResetMasterPassword, this);

    mainSizer->Add(masterPasswordSizer, 0, wxEXPAND | wxALL, 10);

    panel->SetSizer(mainSizer);
    wxBoxSizer* dialogSizer = new wxBoxSizer(wxVERTICAL);
    dialogSizer->Add(panel, 1, wxEXPAND);
    SetSizerAndFit(dialogSizer);
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
    wxFont selectedFont = m_fontPicker->GetSelectedFont();
    GlobalConfig::SetFontName(selectedFont.GetFaceName().ToStdString());
    GlobalConfig::SetFontSize(m_fontSizeSpin->GetValue());
    
    GlobalConfig::SaveSettings();
    
    // Reload translations
    TranslationHelper::Load(wxString::FromUTF8(newLang.c_str()));
    
    EndModal(wxID_OK);
}

void SettingsDialog::OnCancel(wxCommandEvent& event) {
    EndModal(wxID_CANCEL);
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
