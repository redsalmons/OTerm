#ifndef MASTERPASSWORDDIALOG_H
#define MASTERPASSWORDDIALOG_H

#include <wx/dialog.h>
#include <wx/textctrl.h>
#include <wx/stattext.h>
#include <functional>

class MasterPasswordDialog : public wxDialog {
public:
    MasterPasswordDialog(wxWindow* parent, bool isNewPassword);
    virtual ~MasterPasswordDialog();

    wxString GetPassword() const;
    void SetError(const wxString& error);
    void SetPasswordVerifier(std::function<bool(const wxString&, wxString&, bool&)> verifier);

private:
    wxTextCtrl* m_passwordCtrl;
    wxTextCtrl* m_confirmPasswordCtrl;
    wxStaticText* m_errorLabel;
    wxTextCtrl* m_warningText;
    bool m_isNewPassword;
    std::function<bool(const wxString&, wxString&, bool&)> m_passwordVerifier;

    void OnOK(wxCommandEvent& event);
    void OnCancel(wxCommandEvent& event);

    wxDECLARE_EVENT_TABLE();
};

#endif
