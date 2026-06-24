#include "DeviceListPanel.h"
#include "GlobalConfig.h"
#include "TranslationHelper.h"
#include "SSHManager.h"
#include "ConnectionDialog.h"
#include <wx/display.h>
#include <algorithm>

wxDEFINE_EVENT(wxEVT_DEVICE_OPEN_REQUEST, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_DEVICE_DELETE_REQUEST, wxCommandEvent);

DeviceRowPanel::DeviceRowPanel(wxWindow* parent, const DeviceConfig& device, const std::string& deviceId, int index, float dpiScale)
    : wxPanel(parent, wxID_ANY), m_deviceId(deviceId), m_device(device) {

    SetBackgroundColour(wxColour(20, 20, 20));

    wxBoxSizer* rowSizer = new wxBoxSizer(wxHORIZONTAL);

    // Index (sequential number)
    wxStaticText* indexText = new wxStaticText(this, wxID_ANY, wxString::Format("%d", index));
    indexText->SetForegroundColour(wxColour(255, 255, 255));
    indexText->SetMinSize(wxSize(static_cast<int>(30 * dpiScale), -1));
    rowSizer->Add(indexText, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);

    // Device name
    wxStaticText* nameText = new wxStaticText(this, wxID_ANY, wxString::FromUTF8(device.name.c_str()));
    nameText->SetForegroundColour(wxColour(255, 255, 255));
    nameText->SetMinSize(wxSize(static_cast<int>(100 * dpiScale), -1));
    rowSizer->Add(nameText, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);

    // Category
    wxStaticText* categoryText = new wxStaticText(this, wxID_ANY, wxString::FromUTF8(device.group.c_str()));
    categoryText->SetForegroundColour(wxColour(255, 255, 255));
    categoryText->SetMinSize(wxSize(static_cast<int>(133 * dpiScale), -1));
    rowSizer->Add(categoryText, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);

    // Address
    wxStaticText* addressText = new wxStaticText(this, wxID_ANY, wxString::FromUTF8(device.address.c_str()));
    addressText->SetForegroundColour(wxColour(255, 255, 255));
    addressText->SetMinSize(wxSize(static_cast<int>(133 * dpiScale), -1));
    rowSizer->Add(addressText, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);

    // Port
    wxString portStr = wxString::FromUTF8(device.port.c_str());
    portStr.Replace(":", "");
    wxStaticText* portText = new wxStaticText(this, wxID_ANY, portStr);
    portText->SetForegroundColour(wxColour(255, 255, 255));
    portText->SetMinSize(wxSize(static_cast<int>(33 * dpiScale), -1));
    rowSizer->Add(portText, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);

    // Auth type
    wxString authType;
    if (device.auth_method == "password") {
        authType = TranslationHelper::Tr("passwordAuth");
    } else if (device.auth_method == "key") {
        authType = TranslationHelper::Tr("keyAuth");
    } else {
        authType = TranslationHelper::Tr("noAuth");
    }
    wxStaticText* authText = new wxStaticText(this, wxID_ANY, authType);
    authText->SetForegroundColour(wxColour(255, 255, 255));
    authText->SetMinSize(wxSize(static_cast<int>(100 * dpiScale), -1));
    rowSizer->Add(authText, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);

    // Operation column with both buttons
    wxPanel* operationPanel = new wxPanel(this, wxID_ANY);
    operationPanel->SetBackgroundColour(wxColour(20, 20, 20));
    wxBoxSizer* operationSizer = new wxBoxSizer(wxHORIZONTAL);

    wxButton* openButton = new wxButton(operationPanel, wxID_ANY, TranslationHelper::Tr("open"));
    openButton->SetBackgroundColour(wxColour(60, 100, 180));
    openButton->SetForegroundColour(wxColour(255, 255, 255));
    openButton->SetMinSize(wxSize(static_cast<int>(55 * dpiScale), static_cast<int>(25 * dpiScale)));
    openButton->Bind(wxEVT_BUTTON, &DeviceRowPanel::OnOpenButton, this);
    operationSizer->Add(openButton, 0, wxALL, 2);

    wxButton* deleteButton = new wxButton(operationPanel, wxID_ANY, TranslationHelper::Tr("delete"));
    deleteButton->SetBackgroundColour(wxColour(180, 60, 60));
    deleteButton->SetForegroundColour(wxColour(255, 255, 255));
    deleteButton->SetMinSize(wxSize(static_cast<int>(55 * dpiScale), static_cast<int>(25 * dpiScale)));
    deleteButton->Bind(wxEVT_BUTTON, &DeviceRowPanel::OnDeleteButton, this);
    operationSizer->Add(deleteButton, 0, wxALL, 2);

    operationPanel->SetSizer(operationSizer);
    operationPanel->SetMinSize(wxSize(static_cast<int>(120 * dpiScale), -1));
    rowSizer->Add(operationPanel, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);

    SetSizer(rowSizer);
}

void DeviceRowPanel::OnOpenButton(wxCommandEvent& event) {
    wxCommandEvent openEvent(wxEVT_DEVICE_OPEN_REQUEST);
    openEvent.SetString(wxString::FromUTF8(m_deviceId.c_str()));
    openEvent.SetEventObject(this);
    // Send to top-level window (AppWindow)
    wxWindow* topWindow = wxTheApp->GetTopWindow();
    if (topWindow) {
        wxPostEvent(topWindow, openEvent);
    }
}

void DeviceRowPanel::OnDeleteButton(wxCommandEvent& event) {
    wxCommandEvent deleteEvent(wxEVT_DEVICE_DELETE_REQUEST);
    deleteEvent.SetString(wxString::FromUTF8(m_deviceId.c_str()));
    deleteEvent.SetEventObject(this);
    // Send to top-level window (AppWindow)
    wxWindow* topWindow = wxTheApp->GetTopWindow();
    if (topWindow) {
        wxPostEvent(topWindow, deleteEvent);
    }
}

DeviceListPanel::DeviceListPanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY), m_dpiScale(1.0f) {

    // Get DPI scale factor
#ifndef __WXMAC__
    if (GetHandle()) {
        m_dpiScale = GetDPIScaleFactor();
    } else {
        int screenNum = wxDisplay::GetFromWindow(this);
        if (screenNum != wxNOT_FOUND) {
            wxDisplay display(screenNum);
            int dpi = display.GetPPI().GetWidth();
            m_dpiScale = static_cast<float>(dpi) / 96.0f;
        }
    }
    if (m_dpiScale <= 0.0f) m_dpiScale = 1.0f;
#endif

    SetBackgroundColour(wxColour(10, 10, 10));

    // Create main sizer for centering
    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

    // Calculate DPI-scaled dimensions
    int panelWidth = static_cast<int>(760 * m_dpiScale);
    int searchWidth = static_cast<int>(340 * m_dpiScale);
    int buttonWidth = static_cast<int>(35 * m_dpiScale); // Reduced from 70 to 35
    int scrollHeight = static_cast<int>(500 * m_dpiScale);

    // Calculate font height for control height
    wxFont font(static_cast<int>(12 * m_dpiScale), wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
    wxScreenDC dc;
    dc.SetFont(font);
    int controlHeight = dc.GetCharHeight()+4; // Add some padding

    // Create a centered panel with DPI-scaled width
    wxPanel* centerPanel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(panelWidth, -1));
    centerPanel->SetBackgroundColour(wxColour(10, 10, 10));
    centerPanel->SetMinSize(wxSize(panelWidth, -1));
    centerPanel->SetMaxSize(wxSize(panelWidth, -1));
    wxBoxSizer* centerSizer = new wxBoxSizer(wxVERTICAL);

    // Horizontal sizer for search box and add button
    wxBoxSizer* searchSizer = new wxBoxSizer(wxHORIZONTAL);

    // Search box with DPI-scaled dimensions
    m_searchCtrl = new wxSearchCtrl(centerPanel, ID_SEARCH_CTRL, wxEmptyString,
                                   wxDefaultPosition, wxSize(searchWidth, controlHeight),
                                   wxTE_PROCESS_ENTER | wxBORDER_SIMPLE);
    m_searchCtrl->ShowSearchButton(false); // Hide built-in search button to reduce left margin
    m_searchCtrl->SetDescriptiveText(TranslationHelper::Tr("searchHint"));
    m_searchCtrl->SetBackgroundColour(wxColour(0, 0, 0));
    m_searchCtrl->SetForegroundColour(wxColour(255, 255, 255));
    m_searchCtrl->SetFont(wxFont(static_cast<int>(12 * m_dpiScale), wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));

    // Add button with DPI-scaled dimensions
    m_addButton = new wxButton(centerPanel, ID_ADD_BUTTON, "+",
                               wxDefaultPosition, wxSize(buttonWidth, controlHeight));
    m_addButton->SetBackgroundColour(wxColour(60, 100, 180));
    m_addButton->SetForegroundColour(wxColour(255, 255, 255));
    m_addButton->SetFont(wxFont(static_cast<int>(14 * m_dpiScale), wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));

    // Add to search sizer
    searchSizer->Add(m_searchCtrl, 1, wxRIGHT | wxALIGN_CENTER_VERTICAL);
    searchSizer->Add(m_addButton, 0, wxEXPAND | wxALIGN_CENTER_VERTICAL);

    // Device list - use wxScrolledWindow with DPI-scaled dimensions
    m_scrolledWindow = new wxScrolledWindow(centerPanel, wxID_ANY, wxDefaultPosition, wxSize(panelWidth, scrollHeight));
    m_scrolledWindow->SetBackgroundColour(wxColour(10, 10, 10));
    m_scrolledWindow->SetScrollRate(0, static_cast<int>(30 * m_dpiScale)); // Vertical scroll only

    // Content panel inside scrolled window
    m_contentPanel = new wxPanel(m_scrolledWindow, wxID_ANY);
    m_contentPanel->SetBackgroundColour(wxColour(10, 10, 10));
    wxBoxSizer* contentSizer = new wxBoxSizer(wxVERTICAL);
    m_contentPanel->SetSizer(contentSizer);

    m_scrolledWindow->SetSizer(new wxBoxSizer(wxVERTICAL));
    m_scrolledWindow->GetSizer()->Add(m_contentPanel, 1, wxEXPAND);
    m_scrolledWindow->FitInside();

    // Add to center sizer
    centerSizer->Add(searchSizer, 0, wxALL, 10);
    centerSizer->Add(m_scrolledWindow, 1, wxALL, 10);
    centerPanel->SetSizer(centerSizer);

    // Add stretchable space above and below to center vertically
    mainSizer->AddStretchSpacer(1);
    mainSizer->Add(centerPanel, 0, wxALIGN_CENTER_HORIZONTAL);
    mainSizer->AddStretchSpacer(1);

    SetSizer(mainSizer);

    // Bind events
    m_searchCtrl->Bind(wxEVT_TEXT_ENTER, &DeviceListPanel::OnSearch, this);
    m_searchCtrl->Bind(wxEVT_TEXT, &DeviceListPanel::OnSearch, this);
    m_searchCtrl->Bind(wxEVT_SET_FOCUS, &DeviceListPanel::OnSearchFocus, this);
    m_searchCtrl->Bind(wxEVT_KILL_FOCUS, &DeviceListPanel::OnSearchKillFocus, this);
    m_addButton->Bind(wxEVT_BUTTON, &DeviceListPanel::OnAddDevice, this);

    // Load devices
    LoadDevices();
    RefreshDeviceList();
}

void DeviceListPanel::LoadDevices() {
    m_devices = DeviceConfig::LoadFromFile();
}

void DeviceListPanel::RefreshDeviceList(const std::string& filter) {
    std::string lowerFilter = filter;
    std::transform(lowerFilter.begin(), lowerFilter.end(), lowerFilter.begin(), ::tolower);

    std::vector<DeviceConfig> filteredDevices;
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
        filteredDevices.push_back(device);
    }

    // Clear existing row panels
    for (auto* rowPanel : m_rowPanels) {
        rowPanel->Destroy();
    }
    m_rowPanels.clear();

    // Create new row panels
    wxBoxSizer* contentSizer = dynamic_cast<wxBoxSizer*>(m_contentPanel->GetSizer());
    if (contentSizer) {
        contentSizer->Clear(false); // Don't delete children, we handle that above

        // Add header row
        wxPanel* headerPanel = new wxPanel(m_contentPanel, wxID_ANY);
        headerPanel->SetBackgroundColour(wxColour(60, 100, 180));
        wxBoxSizer* headerSizer = new wxBoxSizer(wxHORIZONTAL);

        // Header labels with DPI scaling
        wxStaticText* indexHeader = new wxStaticText(headerPanel, wxID_ANY, TranslationHelper::Tr("index"));
        indexHeader->SetForegroundColour(wxColour(255, 255, 255));
        indexHeader->SetMinSize(wxSize(static_cast<int>(30 * m_dpiScale), -1));
        headerSizer->Add(indexHeader, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);

        wxStaticText* nameHeader = new wxStaticText(headerPanel, wxID_ANY, TranslationHelper::Tr("deviceNameHeader"));
        nameHeader->SetForegroundColour(wxColour(255, 255, 255));
        nameHeader->SetMinSize(wxSize(static_cast<int>(100 * m_dpiScale), -1));
        headerSizer->Add(nameHeader, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);

        wxStaticText* categoryHeader = new wxStaticText(headerPanel, wxID_ANY, TranslationHelper::Tr("category"));
        categoryHeader->SetForegroundColour(wxColour(255, 255, 255));
        categoryHeader->SetMinSize(wxSize(static_cast<int>(133 * m_dpiScale), -1));
        headerSizer->Add(categoryHeader, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);

        wxStaticText* addressHeader = new wxStaticText(headerPanel, wxID_ANY, TranslationHelper::Tr("address"));
        addressHeader->SetForegroundColour(wxColour(255, 255, 255));
        addressHeader->SetMinSize(wxSize(static_cast<int>(133 * m_dpiScale), -1));
        headerSizer->Add(addressHeader, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);

        wxStaticText* portHeader = new wxStaticText(headerPanel, wxID_ANY, TranslationHelper::Tr("port"));
        portHeader->SetForegroundColour(wxColour(255, 255, 255));
        portHeader->SetMinSize(wxSize(static_cast<int>(33 * m_dpiScale), -1));
        headerSizer->Add(portHeader, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);

        wxStaticText* authHeader = new wxStaticText(headerPanel, wxID_ANY, TranslationHelper::Tr("authType"));
        authHeader->SetForegroundColour(wxColour(255, 255, 255));
        authHeader->SetMinSize(wxSize(static_cast<int>(100 * m_dpiScale), -1));
        headerSizer->Add(authHeader, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);

        wxStaticText* operationHeader = new wxStaticText(headerPanel, wxID_ANY, TranslationHelper::Tr("operation"));
        operationHeader->SetForegroundColour(wxColour(255, 255, 255));
        operationHeader->SetMinSize(wxSize(static_cast<int>(120 * m_dpiScale), -1));
        headerSizer->Add(operationHeader, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);

        headerPanel->SetSizer(headerSizer);
        contentSizer->Add(headerPanel, 0, wxEXPAND | wxALL, 2);

        for (size_t i = 0; i < filteredDevices.size(); ++i) {
            DeviceRowPanel* rowPanel = new DeviceRowPanel(
                m_contentPanel,
                filteredDevices[i],
                filteredDevices[i].id,
                static_cast<int>(i + 1),
                m_dpiScale
            );
            contentSizer->Add(rowPanel, 0, wxEXPAND | wxALL, 2);
            m_rowPanels.push_back(rowPanel);
        }

        contentSizer->Layout();
        m_contentPanel->FitInside();
        m_scrolledWindow->FitInside();
    }
}

void DeviceListPanel::OnSearch(wxCommandEvent& event) {
    std::string filter = m_searchCtrl->GetValue().ToStdString();
    // If filter is placeholder text, treat as empty
    if (filter == TranslationHelper::Tr("searchHint").ToStdString()) {
        filter.clear();
    }
    RefreshDeviceList(filter);
}

void DeviceListPanel::OnSearchFocus(wxFocusEvent& event) {
    wxString currentText = m_searchCtrl->GetValue();
    if (currentText == TranslationHelper::Tr("searchHint")) {
        m_searchCtrl->SetValue(wxEmptyString);
    }
    event.Skip();
}

void DeviceListPanel::OnSearchKillFocus(wxFocusEvent& event) {
    wxString currentText = m_searchCtrl->GetValue();
    if (currentText.IsEmpty()) {
        m_searchCtrl->SetValue(TranslationHelper::Tr("searchHint"));
    }
    event.Skip();
}


void DeviceListPanel::OnAddDevice(wxCommandEvent& event) {
    ConnectionDialog dialog(this, TranslationHelper::Tr("addDevice"), true);  // disable connect button
    int result = dialog.ShowModal();
    // Reload devices from file after dialog closes to get the latest state
    LoadDevices();
    RefreshDeviceList();
}

void DeviceListPanel::DeleteDeviceById(const std::string& deviceId) {
    auto it = std::find_if(m_devices.begin(), m_devices.end(),
        [&deviceId](const DeviceConfig& device) {
            return device.id == deviceId;
        });

    if (it != m_devices.end()) {
        m_devices.erase(it);
        DeviceConfig::SaveToFile(m_devices);
        RefreshDeviceList();
    }
}
