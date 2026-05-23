#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <wx/dialog.h>
#include <wx/choice.h>

class SettingsDialog : public wxDialog {
public:
    SettingsDialog(wxWindow* parent);
    virtual ~SettingsDialog();

private:
    wxChoice* m_languageChoice;
    
    void OnOK(wxCommandEvent& event);
    void OnCancel(wxCommandEvent& event);
    
    wxDECLARE_EVENT_TABLE();
};

#endif
