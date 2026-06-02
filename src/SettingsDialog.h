#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <wx/dialog.h>
#include <wx/choice.h>
#include <wx/button.h>
#include <wx/spinctrl.h>
#include <wx/textctrl.h>
#include <wx/stattext.h>

class SettingsDialog : public wxDialog {
public:
    SettingsDialog(wxWindow* parent);
    virtual ~SettingsDialog();

private:
    wxChoice* m_languageChoice;
    wxButton* m_fontButton;
    wxStaticText* m_fontDisplayText;
    wxSpinCtrl* m_fontSizeSpin;

    wxTextCtrl* m_masterPassword1;
    wxTextCtrl* m_masterPassword2;
    wxButton* m_masterPasswordSaveButton;

    void OnOK(wxCommandEvent& event);
    void OnCancel(wxCommandEvent& event);
    void OnSetMasterPassword(wxCommandEvent& event);
    void OnSaveMasterPassword(wxCommandEvent& event);
    void OnResetMasterPassword(wxCommandEvent& event);
    void OnFontButtonClicked(wxCommandEvent& event);

    wxDECLARE_EVENT_TABLE();
};

#endif
