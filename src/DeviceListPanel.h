#pragma once

#include <wx/wx.h>
#include <wx/scrolwin.h>
#include <wx/srchctrl.h>
#include <vector>
#include "DeviceConfig.h"

wxDECLARE_EVENT(wxEVT_DEVICE_OPEN_REQUEST, wxCommandEvent);
wxDECLARE_EVENT(wxEVT_DEVICE_DELETE_REQUEST, wxCommandEvent);

class DeviceRowPanel : public wxPanel {
public:
    DeviceRowPanel(wxWindow* parent, const DeviceConfig& device, const std::string& deviceId, int index, float dpiScale);
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
    void LoadDevices();
    void RefreshDeviceList(const std::string& filter = "");

private:
    void OnSearch(wxCommandEvent& event);
    void OnAddDevice(wxCommandEvent& event);
    void OnSearchFocus(wxFocusEvent& event);
    void OnSearchKillFocus(wxFocusEvent& event);

    wxSearchCtrl* m_searchCtrl;
    wxButton* m_addButton;
    wxScrolledWindow* m_scrolledWindow;
    wxPanel* m_contentPanel;
    std::vector<DeviceConfig> m_devices;
    std::vector<DeviceRowPanel*> m_rowPanels;
    float m_dpiScale;

    enum {
        ID_SEARCH_CTRL = wxID_HIGHEST + 1,
        ID_ADD_BUTTON
    };
};
