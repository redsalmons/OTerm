#include "DeviceListPanel.h"
#include "GlobalConfig.h"
#include "TranslationHelper.h"
#include <algorithm>

wxDEFINE_EVENT(wxEVT_DEVICE_OPEN_REQUEST, wxCommandEvent);

DeviceListPanel::DeviceListPanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY) {
    
    SetBackgroundColour(wxColour(10, 10, 10));
    
    // Create main sizer for centering
    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
    
    // Create a centered panel with fixed width
    wxPanel* centerPanel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(760, -1));
    centerPanel->SetBackgroundColour(wxColour(10, 10, 10));
    centerPanel->SetMinSize(wxSize(760, -1));
    centerPanel->SetMaxSize(wxSize(760, -1));
    wxBoxSizer* centerSizer = new wxBoxSizer(wxVERTICAL);
    
    // Search box
    m_searchCtrl = new wxTextCtrl(centerPanel, ID_SEARCH_CTRL, wxEmptyString, 
                                   wxDefaultPosition, wxSize(760, 30), 
                                   wxTE_PROCESS_ENTER | wxBORDER_SIMPLE);
    m_searchCtrl->SetHint(TranslationHelper::Tr("searchHint"));
    m_searchCtrl->SetBackgroundColour(wxColour(0, 0, 0));
    m_searchCtrl->SetForegroundColour(wxColour(255, 255, 255));
    
    // Device list
    m_deviceList = new wxGrid(centerPanel, ID_DEVICE_LIST, wxDefaultPosition, wxSize(760, 400));
    m_deviceList->CreateGrid(0, 7);
    m_deviceList->SetBackgroundColour(wxColour(10, 10, 10));
    m_deviceList->SetGridLineColour(wxColour(50, 50, 50));
    m_deviceList->SetDefaultCellBackgroundColour(wxColour(10, 10, 10));
    m_deviceList->SetDefaultCellTextColour(wxColour(200, 200, 200));
    m_deviceList->SetDefaultRowSize(30);
    m_deviceList->SetDefaultCellAlignment(wxALIGN_LEFT, wxALIGN_CENTER);
    m_deviceList->SetColLabelSize(35);
    m_deviceList->SetLabelBackgroundColour(wxColour(60, 100, 180));
    m_deviceList->SetLabelTextColour(wxColour(255, 255, 255));
    m_deviceList->SetLabelFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    m_deviceList->EnableGridLines(true);
    m_deviceList->SetRowLabelSize(0);
    m_deviceList->SetColLabelAlignment(wxALIGN_CENTER, wxALIGN_CENTER);
    m_deviceList->DisableDragColSize();
    m_deviceList->DisableDragRowSize();
    m_deviceList->DisableDragGridSize();
    m_deviceList->SetSelectionMode(wxGrid::wxGridSelectNone);
    m_deviceList->SetMargins(0, 0);
    m_deviceList->SetScrollbars(0, 20, 0, 100);
    
    // Set column labels and widths
    m_deviceList->SetColLabelValue(0, TranslationHelper::Tr("index"));
    m_deviceList->SetColSize(0, 60);
    m_deviceList->SetColLabelValue(1, TranslationHelper::Tr("deviceName"));
    m_deviceList->SetColSize(1, 150);
    m_deviceList->SetColLabelValue(2, TranslationHelper::Tr("category"));
    m_deviceList->SetColSize(2, 100);
    m_deviceList->SetColLabelValue(3, TranslationHelper::Tr("address"));
    m_deviceList->SetColSize(3, 150);
    m_deviceList->SetColLabelValue(4, TranslationHelper::Tr("ip"));
    m_deviceList->SetColSize(4, 120);
    m_deviceList->SetColLabelValue(5, TranslationHelper::Tr("authType"));
    m_deviceList->SetColSize(5, 100);
    m_deviceList->SetColLabelValue(6, TranslationHelper::Tr("operation"));
    m_deviceList->SetColSize(6, 80);
    
    // Add to center sizer
    centerSizer->Add(m_searchCtrl, 0, wxALL, 10);
    centerSizer->Add(m_deviceList, 1, wxALL, 10);
    centerPanel->SetSizer(centerSizer);
    
    // Add stretchable space above and below to center vertically
    mainSizer->AddStretchSpacer(1);
    mainSizer->Add(centerPanel, 0, wxALIGN_CENTER_HORIZONTAL);
    mainSizer->AddStretchSpacer(1);
    
    SetSizer(mainSizer);
    
    // Bind events
    m_searchCtrl->Bind(wxEVT_TEXT_ENTER, &DeviceListPanel::OnSearch, this);
    m_searchCtrl->Bind(wxEVT_TEXT, &DeviceListPanel::OnSearch, this);
    m_deviceList->Bind(wxEVT_GRID_CELL_LEFT_CLICK, &DeviceListPanel::OnGridCellLeftClick, this);
    
    // Load devices
    LoadDevices();
    RefreshDeviceList();
}

void DeviceListPanel::LoadDevices() {
    m_devices = DeviceConfig::LoadFromFile();
}

void DeviceListPanel::RefreshDeviceList(const std::string& filter) {
    // Clear existing rows
    if (m_deviceList->GetNumberRows() > 0) {
        m_deviceList->DeleteRows(0, m_deviceList->GetNumberRows());
    }
    
    std::string lowerFilter = filter;
    std::transform(lowerFilter.begin(), lowerFilter.end(), lowerFilter.begin(), ::tolower);
    
    int index = 1;
    for (const auto& device : m_devices) {
        // Filter check
        if (!filter.empty()) {
            std::string name = device.name;
            std::string id = device.id;
            std::string address = device.address;
            std::string group = device.group;
            
            std::transform(name.begin(), name.end(), name.begin(), ::tolower);
            std::transform(id.begin(), id.end(), id.begin(), ::tolower);
            std::transform(address.begin(), address.end(), address.begin(), ::tolower);
            std::transform(group.begin(), group.end(), group.begin(), ::tolower);
            
            if (name.find(lowerFilter) == std::string::npos &&
                id.find(lowerFilter) == std::string::npos &&
                address.find(lowerFilter) == std::string::npos &&
                group.find(lowerFilter) == std::string::npos) {
                continue;
            }
        }
        
        // Add row
        m_deviceList->AppendRows(1);
        int row = m_deviceList->GetNumberRows() - 1;
        
        m_deviceList->SetCellValue(row, 0, std::to_string(index));
        m_deviceList->SetCellValue(row, 1, wxString::FromUTF8(device.name.c_str()));
        m_deviceList->SetCellValue(row, 2, wxString::FromUTF8(device.group.c_str()));
        m_deviceList->SetCellValue(row, 3, wxString::FromUTF8(device.address.c_str()));
        m_deviceList->SetCellValue(row, 4, wxString::FromUTF8(device.address.c_str()));
        
        // Auth type
        std::string authType;
        if (device.auth_method == "password") {
            authType = TranslationHelper::Tr("passwordAuth");
        } else if (device.auth_method == "key") {
            authType = TranslationHelper::Tr("keyAuth");
        } else {
            authType = TranslationHelper::Tr("noAuth");
        }
        m_deviceList->SetCellValue(row, 5, wxString::FromUTF8(authType.c_str()));
        m_deviceList->SetCellValue(row, 6, TranslationHelper::Tr("open"));
        
        // Store device index in row label
        m_deviceList->SetRowLabelValue(row, std::to_string(index - 1));
        
        index++;
    }
}

void DeviceListPanel::OnSearch(wxCommandEvent& event) {
    std::string filter = m_searchCtrl->GetValue().ToStdString();
    RefreshDeviceList(filter);
}

void DeviceListPanel::OnGridCellLeftClick(wxGridEvent& event) {
    int row = event.GetRow();
    int col = event.GetCol();
    
    // Check if click is in operation column (column 6)
    if (col == 6 && row >= 0 && row < (int)m_devices.size()) {
        // Get device index from row label
        wxString label = m_deviceList->GetRowLabelValue(row);
        long deviceIndex;
        if (label.ToLong(&deviceIndex) && deviceIndex >= 0 && deviceIndex < (long)m_devices.size()) {
            wxCommandEvent openEvent(wxEVT_DEVICE_OPEN_REQUEST);
            openEvent.SetEventObject(this);
            openEvent.SetInt(deviceIndex);
            wxPostEvent(GetParent(), openEvent);
        }
    }
}
