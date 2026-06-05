#pragma once

#include <wx/wx.h>
#include <wx/grid.h>
#include <vector>
#include "DeviceConfig.h"

wxDECLARE_EVENT(wxEVT_DEVICE_OPEN_REQUEST, wxCommandEvent);

class DeviceListPanel : public wxPanel {
public:
    DeviceListPanel(wxWindow* parent);
    
private:
    void OnSearch(wxCommandEvent& event);
    void OnGridCellLeftClick(wxGridEvent& event);
    void LoadDevices();
    void RefreshDeviceList(const std::string& filter = "");
    
    wxTextCtrl* m_searchCtrl;
    wxGrid* m_deviceList;
    std::vector<DeviceConfig> m_devices;
    
    enum {
        ID_SEARCH_CTRL = wxID_HIGHEST + 1,
        ID_DEVICE_LIST,
        ID_OPEN_BUTTON
    };
};
