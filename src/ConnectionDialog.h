#pragma once

#include <wx/wx.h>

#include <wx/treectrl.h>
#include <map>
#include <vector>
#include "DeviceConfig.h"

class DeviceTreeItemData : public wxTreeItemData {
public:
    DeviceTreeItemData(size_t index) : m_index(index) {}
    size_t GetIndex() const { return m_index; }
private:
    size_t m_index;
};

class ConnectionDialog : public wxDialog {
public:
    ConnectionDialog(wxWindow* parent, const wxString& title);

    DeviceConfig GetSelectedDevice() const { return m_selectedDevice; }

private:
    void OnTreeSelectionChanged(wxTreeEvent& event);
    void OnSave(wxCommandEvent& event);
    void OnConnect(wxCommandEvent& event);
    void OnCancel(wxCommandEvent& event);
    void OnAuthMethodChanged(wxCommandEvent& event);
    void OnKeyBrowse(wxCommandEvent& event);

    void LoadConfig();
    void SaveConfig();
    void RebuildTree();
    void PopulateForm(const DeviceConfig& device);
    void UpdatePasswordFieldVisibility();

    wxTreeCtrl* m_treeCtrl;
    wxTextCtrl* m_nameCtrl;
    wxTextCtrl* m_usernameCtrl;
    wxTextCtrl* m_addressCtrl;
    wxTextCtrl* m_portCtrl;
    wxTextCtrl* m_groupCtrl;
    wxChoice* m_authChoice;
    wxTextCtrl* m_passwordCtrl;
    wxTextCtrl* m_keyTextCtrl;
    wxButton* m_keyBrowseButton;
    wxButton* m_connectButton;

    std::vector<DeviceConfig> m_devices;
    DeviceConfig m_currentDevice;
    DeviceConfig m_selectedDevice;
    wxTreeItemId m_rootId;
};
