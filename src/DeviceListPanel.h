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
    void SetFocusToSearch();
    void ApplyUiPalette();

private:
    void OnSearch(wxCommandEvent& event);
    void OnAddDevice(wxCommandEvent& event);

    wxPanel* m_centerPanel = nullptr;
    wxSearchCtrl* m_searchCtrl = nullptr;
    wxButton* m_addButton = nullptr;
    wxStaticText* m_statusText = nullptr;
    wxScrolledWindow* m_scrolledWindow = nullptr;
    wxPanel* m_contentPanel = nullptr;
    std::vector<DeviceConfig> m_devices;
    std::vector<DeviceRowPanel*> m_rowPanels;
    float m_dpiScale;

    // Polls the OS appearance: wxEVT_SYS_COLOUR_CHANGED is unreliable on
    // wxMSW, so the panel re-themes itself when the setting flips.
    wxTimer m_appearanceTimer;
    bool m_appWasDark = false;

    enum {
        ID_SEARCH_CTRL = wxID_HIGHEST + 1,
        ID_ADD_BUTTON
    };
};
