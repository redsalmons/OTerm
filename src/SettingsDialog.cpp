#include "SettingsDialog.h"
#include "TranslationHelper.h"
#include "GlobalConfig.h"
#include <wx/sizer.h>
#include <wx/stattext.h>

wxBEGIN_EVENT_TABLE(SettingsDialog, wxDialog)
    EVT_BUTTON(wxID_OK, SettingsDialog::OnOK)
    EVT_BUTTON(wxID_CANCEL, SettingsDialog::OnCancel)
wxEND_EVENT_TABLE()

SettingsDialog::SettingsDialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, TranslationHelper::Tr("settings"), wxDefaultPosition, wxSize(400, 200)) {
    
    wxPanel* panel = new wxPanel(this);
    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
    
    // Language selection
    wxBoxSizer* languageSizer = new wxBoxSizer(wxHORIZONTAL);
    languageSizer->Add(new wxStaticText(panel, wxID_ANY, TranslationHelper::Tr("language")), 0, wxALIGN_CENTER_VERTICAL | wxALL, 10);
    
    wxString languageChoices[] = { TranslationHelper::Tr("chinese"), TranslationHelper::Tr("english") };
    m_languageChoice = new wxChoice(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, 2, languageChoices);
    
    // Set current language
    std::string currentLang = GlobalConfig::GetLanguage();
    if (currentLang == "zh_CN") {
        m_languageChoice->SetSelection(0);
    } else {
        m_languageChoice->SetSelection(1);
    }
    
    languageSizer->Add(m_languageChoice, 1, wxEXPAND | wxALL, 10);
    mainSizer->Add(languageSizer, 0, wxEXPAND);
    
    // Buttons
    wxBoxSizer* buttonSizer = new wxBoxSizer(wxHORIZONTAL);
    buttonSizer->Add(new wxButton(panel, wxID_OK, TranslationHelper::Tr("save")), 0, wxALL, 5);
    buttonSizer->Add(new wxButton(panel, wxID_CANCEL, TranslationHelper::Tr("cancel")), 0, wxALL, 5);
    mainSizer->Add(buttonSizer, 0, wxALIGN_RIGHT | wxALL, 10);
    
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
    GlobalConfig::SaveSettings();
    
    // Reload translations
    TranslationHelper::Load(wxString::FromUTF8(newLang.c_str()));
    
    EndModal(wxID_OK);
}

void SettingsDialog::OnCancel(wxCommandEvent& event) {
    EndModal(wxID_CANCEL);
}
