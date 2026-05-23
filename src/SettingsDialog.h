#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <wx/dialog.h>
#include <wx/choice.h>
#include <wx/fontpicker.h>
#include <wx/spinctrl.h>

class SettingsDialog : public wxDialog {
public:
    SettingsDialog(wxWindow* parent);
    virtual ~SettingsDialog();

private:
    wxChoice* m_languageChoice;
    wxFontPickerCtrl* m_fontPicker;
    wxSpinCtrl* m_fontSizeSpin;
    
    void OnOK(wxCommandEvent& event);
    void OnCancel(wxCommandEvent& event);
    
    wxDECLARE_EVENT_TABLE();
};

#endif
