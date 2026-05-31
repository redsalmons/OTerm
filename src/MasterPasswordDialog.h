#ifndef MASTERPASSWORDDIALOG_H
#define MASTERPASSWORDDIALOG_H

#include <wx/dialog.h>
#include <wx/textctrl.h>

class MasterPasswordDialog : public wxDialog {
public:
    MasterPasswordDialog(wxWindow* parent, bool isNewPassword);
    virtual ~MasterPasswordDialog();

    wxString GetPassword() const;

private:
    wxTextCtrl* m_passwordCtrl;
    wxTextCtrl* m_confirmPasswordCtrl;
    bool m_isNewPassword;

    void OnOK(wxCommandEvent& event);
    void OnCancel(wxCommandEvent& event);

    wxDECLARE_EVENT_TABLE();
};

#endif
