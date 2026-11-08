#include "DeviceListPanel.h"
#include "GlobalConfig.h"
#include "TranslationHelper.h"
#include "SSHManager.h"
#include "ConnectionDialog.h"
#include "UiColors.h"
#include <wx/display.h>
#include <wx/statline.h>
#include <algorithm>

wxDEFINE_EVENT(wxEVT_DEVICE_OPEN_REQUEST, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_DEVICE_DELETE_REQUEST, wxCommandEvent);

DeviceRowPanel::DeviceRowPanel(wxWindow* parent, const DeviceConfig& device, const std::string& deviceId, int index, float dpiScale)
    : wxPanel(parent, wxID_ANY), m_deviceId(deviceId), m_device(device) {

    const UiPalette pal = CurrentUiPalette();

    // Alternating row colours for a subtle zebra effect.
    const wxColour rowBg = (index % 2) ? pal.bgRowOdd : pal.bgRowEven;

    SetBackgroundColour(rowBg);
    SetMinSize(wxSize(-1, static_cast<int>(36 * dpiScale)));

    wxBoxSizer* rowSizer = new wxBoxSizer(wxHORIZONTAL);

    auto addCell = [&](const wxString& text, const wxColour& color, bool bold,
                       int minWidth, int proportion, bool ellipsize) {
        wxStaticText* t = new wxStaticText(this, wxID_ANY, text,
            wxDefaultPosition, wxDefaultSize,
            ellipsize ? wxST_ELLIPSIZE_MIDDLE : 0);
        t->SetForegroundColour(color);
        if (bold) t->SetFont(t->GetFont().Bold());
        t->SetMinSize(wxSize(static_cast<int>(minWidth * dpiScale), -1));
        rowSizer->Add(t, proportion, wxLEFT | wxRIGHT | wxALIGN_CENTER_VERTICAL, 6);
        return t;
    };

    // Column proportions must match the header row in RefreshDeviceList.
    addCell(wxString::Format("%d", index), pal.textFaint, false, 24, 0, false);
    addCell(wxString::FromUTF8(device.name.c_str()), pal.textPrimary, true, 90, 1, true);
    addCell(wxString::FromUTF8(device.group.c_str()), pal.textSecondary, false, 80, 0, true);

    addCell(wxString::FromUTF8(device.address.c_str()), pal.textSecondary, false, 110, 1, true);

    wxString portStr = wxString::FromUTF8(device.port.c_str());
    portStr.Replace(":", "");
    addCell(portStr, pal.textSecondary, false, 40, 0, false);

    wxString authType;
    if (device.auth_method == "password") {
        authType = TranslationHelper::Tr("passwordAuth");
    } else if (device.auth_method == "key") {
        authType = TranslationHelper::Tr("keyAuth");
    } else {
        authType = TranslationHelper::Tr("noAuth");
    }
    addCell(authType, pal.textSecondary, false, 76, 0, true);

    // Operation column with both buttons — native button style reads better
    // on the dark rows than hand-picked colours (which macOS ignores anyway).
    wxPanel* operationPanel = new wxPanel(this, wxID_ANY);
    operationPanel->SetBackgroundColour(rowBg);
    wxBoxSizer* operationSizer = new wxBoxSizer(wxHORIZONTAL);

    wxButton* openButton = new wxButton(operationPanel, wxID_ANY, TranslationHelper::Tr("open"));
    openButton->SetMinSize(wxSize(static_cast<int>(52 * dpiScale), static_cast<int>(24 * dpiScale)));
    openButton->Bind(wxEVT_BUTTON, &DeviceRowPanel::OnOpenButton, this);
    operationSizer->Add(openButton, 0, wxALL | wxALIGN_CENTER_VERTICAL, 2);

    wxButton* deleteButton = new wxButton(operationPanel, wxID_ANY, TranslationHelper::Tr("delete"));
    deleteButton->SetMinSize(wxSize(static_cast<int>(52 * dpiScale), static_cast<int>(24 * dpiScale)));
    deleteButton->Bind(wxEVT_BUTTON, &DeviceRowPanel::OnDeleteButton, this);
    operationSizer->Add(deleteButton, 0, wxALL | wxALIGN_CENTER_VERTICAL, 2);

    operationPanel->SetSizer(operationSizer);
    operationPanel->SetMinSize(wxSize(static_cast<int>(116 * dpiScale), -1));
    rowSizer->Add(operationPanel, 0, wxLEFT | wxRIGHT | wxALIGN_CENTER_VERTICAL, 6);

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
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL | wxWANTS_CHARS), m_dpiScale(1.0f) {

    const UiPalette pal = CurrentUiPalette();

    // Enable keyboard input focus
    SetCanFocus(true);

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

    SetBackgroundColour(pal.bgWindow);

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
    wxPanel* centerPanel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(panelWidth, -1),
                                       wxTAB_TRAVERSAL | wxBORDER_SIMPLE);
    centerPanel->SetBackgroundColour(pal.bgWindow);
    centerPanel->SetMinSize(wxSize(panelWidth, -1));
    centerPanel->SetMaxSize(wxSize(panelWidth, -1));
    centerPanel->SetCanFocus(true);
    wxBoxSizer* centerSizer = new wxBoxSizer(wxVERTICAL);

    // Horizontal sizer for search box and add button
    wxBoxSizer* searchSizer = new wxBoxSizer(wxHORIZONTAL);

    // Search box with DPI-scaled dimensions
    m_searchCtrl = new wxSearchCtrl(centerPanel, ID_SEARCH_CTRL, wxEmptyString,
                                   wxDefaultPosition, wxSize(searchWidth, controlHeight),
                                   wxTE_PROCESS_ENTER | wxBORDER_SIMPLE | wxWANTS_CHARS);
    m_searchCtrl->ShowSearchButton(false); // Hide built-in search button to reduce left margin
    m_searchCtrl->SetCanFocus(true);
    m_searchCtrl->SetDescriptiveText(TranslationHelper::Tr("searchPlaceholder"));
    m_searchCtrl->SetBackgroundColour(pal.bgSearch);
    m_searchCtrl->SetForegroundColour(pal.textPrimary);
    m_searchCtrl->SetFont(wxFont(static_cast<int>(12 * m_dpiScale), wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));

    // Add button with DPI-scaled dimensions — keep the native button look,
    // just a slightly bolder "+" glyph.
    m_addButton = new wxButton(centerPanel, ID_ADD_BUTTON, "+",
                               wxDefaultPosition, wxSize(buttonWidth, controlHeight));
    m_addButton->SetFont(wxFont(static_cast<int>(14 * m_dpiScale), wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));

    // Add to search sizer
    searchSizer->Add(m_searchCtrl, 1, wxRIGHT | wxALIGN_CENTER_VERTICAL);
    searchSizer->Add(m_addButton, 0, wxEXPAND | wxALIGN_CENTER_VERTICAL);

    // Device list - use wxScrolledWindow with DPI-scaled dimensions
    m_scrolledWindow = new wxScrolledWindow(centerPanel, wxID_ANY, wxDefaultPosition, wxSize(panelWidth, scrollHeight));
    m_scrolledWindow->SetBackgroundColour(pal.bgContent);
    m_scrolledWindow->SetScrollRate(0, static_cast<int>(30 * m_dpiScale)); // Vertical scroll only

    // Content panel inside scrolled window
    m_contentPanel = new wxPanel(m_scrolledWindow, wxID_ANY);
    m_contentPanel->SetBackgroundColour(pal.bgContent);
    wxBoxSizer* contentSizer = new wxBoxSizer(wxVERTICAL);
    m_contentPanel->SetSizer(contentSizer);

    m_scrolledWindow->SetSizer(new wxBoxSizer(wxVERTICAL));
    m_scrolledWindow->GetSizer()->Add(m_contentPanel, 1, wxEXPAND);
    m_scrolledWindow->FitInside();

    // Status bar: device count, right-aligned under the list.
    m_statusText = new wxStaticText(centerPanel, wxID_ANY, "",
        wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT);
    m_statusText->SetForegroundColour(pal.textFaint);
    m_statusText->SetFont(wxFont(static_cast<int>(11 * m_dpiScale), wxFONTFAMILY_DEFAULT,
                                 wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));

    // Add to center sizer
    centerSizer->Add(searchSizer, 0, wxLEFT | wxRIGHT | wxTOP, 12);
    centerSizer->Add(m_scrolledWindow, 1, wxLEFT | wxRIGHT | wxTOP, 10);
    centerSizer->Add(m_statusText, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
    centerPanel->SetSizer(centerSizer);

    // Add stretchable space above and below to center vertically
    mainSizer->AddStretchSpacer(1);
    mainSizer->Add(centerPanel, 0, wxALIGN_CENTER_HORIZONTAL);
    mainSizer->AddStretchSpacer(1);

    SetSizer(mainSizer);

    // Bind events
    m_searchCtrl->Bind(wxEVT_TEXT_ENTER, &DeviceListPanel::OnSearch, this);
    m_searchCtrl->Bind(wxEVT_TEXT, &DeviceListPanel::OnSearch, this);
    m_addButton->Bind(wxEVT_BUTTON, &DeviceListPanel::OnAddDevice, this);

    // Load devices
    LoadDevices();
    RefreshDeviceList();
}

void DeviceListPanel::LoadDevices() {
    m_devices = DeviceConfig::LoadFromFile();
}

void DeviceListPanel::RefreshDeviceList(const std::string& filter) {
    const UiPalette pal = CurrentUiPalette();
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

        // Add header row — flat dark strip with muted labels, visually
        // separated from the zebra rows below. Column proportions must
        // match DeviceRowPanel so headers stay aligned with the cells.
        wxPanel* headerPanel = new wxPanel(m_contentPanel, wxID_ANY);
        headerPanel->SetBackgroundColour(pal.bgHeader);
        wxBoxSizer* headerSizer = new wxBoxSizer(wxHORIZONTAL);

        const wxColour headerText = pal.textFaint;
        auto addHeader = [&](const wxString& key, int minWidth, int proportion) {
            wxStaticText* t = new wxStaticText(headerPanel, wxID_ANY,
                                               TranslationHelper::Tr(key),
                                               wxDefaultPosition, wxDefaultSize,
                                               wxST_ELLIPSIZE_MIDDLE);
            t->SetForegroundColour(headerText);
            t->SetFont(t->GetFont().Bold());
            t->SetMinSize(wxSize(static_cast<int>(minWidth * m_dpiScale), -1));
            headerSizer->Add(t, proportion, wxLEFT | wxRIGHT | wxALIGN_CENTER_VERTICAL, 6);
        };
        addHeader("index", 24, 0);
        addHeader("deviceNameHeader", 90, 1);
        addHeader("category", 80, 0);
        addHeader("address", 110, 1);
        addHeader("port", 40, 0);
        addHeader("authType", 76, 0);
        addHeader("operation", 116, 0);

        headerPanel->SetSizer(headerSizer);
        contentSizer->Add(headerPanel, 0, wxEXPAND);
        wxStaticLine* headerLine = new wxStaticLine(m_contentPanel, wxID_ANY,
                                                    wxDefaultPosition, wxDefaultSize,
                                                    wxLI_HORIZONTAL);
        headerLine->SetForegroundColour(pal.border);
        contentSizer->Add(headerLine, 0, wxEXPAND);

        if (filteredDevices.empty()) {
            wxStaticText* emptyText = new wxStaticText(m_contentPanel, wxID_ANY,
                filter.empty() ? TranslationHelper::Tr("noDevices")
                               : TranslationHelper::Tr("noSearchResults"),
                wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL);
            emptyText->SetForegroundColour(pal.textFaint);
            emptyText->SetMinSize(wxSize(-1, static_cast<int>(120 * m_dpiScale)));
            contentSizer->Add(emptyText, 1, wxEXPAND | wxTOP, 30);
        }

        for (size_t i = 0; i < filteredDevices.size(); ++i) {
            DeviceRowPanel* rowPanel = new DeviceRowPanel(
                m_contentPanel,
                filteredDevices[i],
                filteredDevices[i].id,
                static_cast<int>(i + 1),
                m_dpiScale
            );
            contentSizer->Add(rowPanel, 0, wxEXPAND);
            m_rowPanels.push_back(rowPanel);
        }

        if (m_statusText) {
            if (filter.empty()) {
                m_statusText->SetLabel(wxString::Format(
                    TranslationHelper::Tr("deviceCount"),
                    static_cast<int>(m_devices.size())));
            } else {
                m_statusText->SetLabel(wxString::Format(
                    TranslationHelper::Tr("deviceCountFiltered"),
                    static_cast<int>(filteredDevices.size()),
                    static_cast<int>(m_devices.size())));
            }
        }

        contentSizer->Layout();
        m_contentPanel->FitInside();
        m_scrolledWindow->FitInside();
    }
}

void DeviceListPanel::OnSearch(wxCommandEvent& event) {
    std::string filter = m_searchCtrl->GetValue().ToStdString();
    RefreshDeviceList(filter);
}

void DeviceListPanel::SetFocusToSearch() {
    if (m_searchCtrl) {
        m_searchCtrl->SetFocus();
    }
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
        // AppWindow already persisted the deletion; reload instead of saving
        // the cached list, which may hold stale credentials
        LoadDevices();
        RefreshDeviceList();
    }
}
