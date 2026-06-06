#pragma once

#include <wx/wx.h>
#include "DeviceConfig.h"

class AddDeviceDialog : public wxDialog {
public:
    AddDeviceDialog(wxWindow* parent);

    DeviceConfig GetDeviceConfig() const { return m_deviceConfig; }

private:
    void OnSave(wxCommandEvent& event);
    void OnCancel(wxCommandEvent& event);
    void OnAuthMethodChanged(wxCommandEvent& event);
    void OnKeyBrowse(wxCommandEvent& event);

    void UpdatePasswordFieldVisibility();

    wxTextCtrl* m_nameCtrl;
    wxTextCtrl* m_usernameCtrl;
    wxTextCtrl* m_addressCtrl;
    wxTextCtrl* m_portCtrl;
    wxTextCtrl* m_groupCtrl;
    wxChoice* m_authChoice;
    wxTextCtrl* m_passwordCtrl;
    wxTextCtrl* m_keyTextCtrl;
    wxButton* m_keyBrowseButton;
    wxButton* m_saveButton;

    DeviceConfig m_deviceConfig;
};
