#pragma once

#include <wx/wx.h>
#include <wx/scrolwin.h>
#include <vector>
#include "DeviceConfig.h"

wxDECLARE_EVENT(wxEVT_DEVICE_OPEN_REQUEST, wxCommandEvent);

class DeviceRowPanel : public wxPanel {
public:
    DeviceRowPanel(wxWindow* parent, const DeviceConfig& device, const std::string& deviceId);
    const std::string& GetDeviceId() const { return m_deviceId; }

private:
    void OnOpenButton(wxCommandEvent& event);
    void OnDeleteButton(wxCommandEvent& event);

    std::string m_deviceId;
    DeviceConfig m_device;
};

class DeviceListPanel : public wxPanel {
public:
    DeviceListPanel(wxWindow* parent);
    void DeleteDeviceById(const std::string& deviceId);

private:
    void OnSearch(wxCommandEvent& event);
    void OnAddDevice(wxCommandEvent& event);
    void OnDeviceDeleteRequest(wxCommandEvent& event);
    void OnSearchFocus(wxFocusEvent& event);
    void OnSearchKillFocus(wxFocusEvent& event);
    void LoadDevices();
    void RefreshDeviceList(const std::string& filter = "");

    wxTextCtrl* m_searchCtrl;
    wxButton* m_addButton;
    wxScrolledWindow* m_scrolledWindow;
    wxPanel* m_contentPanel;
    std::vector<DeviceConfig> m_devices;
    std::vector<DeviceRowPanel*> m_rowPanels;

    enum {
        ID_SEARCH_CTRL = wxID_HIGHEST + 1,
        ID_ADD_BUTTON
    };
};
