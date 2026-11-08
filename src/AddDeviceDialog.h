#pragma once

#include <wx/wx.h>
#include "DeviceConfig.h"

class PaletteChoice;

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
    void ApplyUiPalette();

    wxTextCtrl* m_nameCtrl;
    wxTextCtrl* m_usernameCtrl;
    wxTextCtrl* m_addressCtrl;
    wxTextCtrl* m_portCtrl;
    wxTextCtrl* m_groupCtrl;
    PaletteChoice* m_authChoice;
    wxTextCtrl* m_passwordCtrl;
    wxTextCtrl* m_keyTextCtrl;
    wxButton* m_keyBrowseButton;
    wxButton* m_saveButton;

    DeviceConfig m_deviceConfig;

    // Appearance tracking (poll-based; SYS_COLOUR_CHANGED is unreliable on wxMSW).
    wxTimer m_appearanceTimer;
    bool m_appWasDark = false;
};
