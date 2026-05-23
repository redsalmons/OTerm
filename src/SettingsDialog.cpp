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
    : wxDialog(parent, wxID_ANY, TranslationHelper::Tr("settings"), wxDefaultPosition, wxSize(450, 300)) {
    
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
    
    // Font selection
    wxBoxSizer* fontSizer = new wxBoxSizer(wxHORIZONTAL);
    fontSizer->Add(new wxStaticText(panel, wxID_ANY, TranslationHelper::Tr("font")), 0, wxALIGN_CENTER_VERTICAL | wxALL, 10);
    
    // Set current font
    std::string currentFontName = GlobalConfig::GetFontName();
    int currentFontSize = GlobalConfig::GetFontSize();
    wxFont currentFont(currentFontSize, wxFONTFAMILY_MODERN, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxString::FromUTF8(currentFontName.c_str()));
    
    m_fontPicker = new wxFontPickerCtrl(panel, wxID_ANY, currentFont);
    fontSizer->Add(m_fontPicker, 1, wxEXPAND | wxALL, 10);
    mainSizer->Add(fontSizer, 0, wxEXPAND);
    
    // Font size
    wxBoxSizer* fontSizeSizer = new wxBoxSizer(wxHORIZONTAL);
    fontSizeSizer->Add(new wxStaticText(panel, wxID_ANY, TranslationHelper::Tr("fontSize")), 0, wxALIGN_CENTER_VERTICAL | wxALL, 10);
    
    m_fontSizeSpin = new wxSpinCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 8, 72, currentFontSize);
    fontSizeSizer->Add(m_fontSizeSpin, 0, wxALL, 10);
    mainSizer->Add(fontSizeSizer, 0, wxEXPAND);
    
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
