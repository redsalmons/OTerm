#include "AppWindow.h"
#include "GlobalConfig.h"
#include "TranslationHelper.h"
#include "MasterPasswordDialog.h"
#include "LocalTerminalThread.h"
#include <iostream>
#include <wx/simplebook.h>
#include "ConnectionDialog.h"
#include "DeviceConfig.h"
#include "TermGLCanvas.h"
#include "DeviceListPanel.h"
#include "SSHManager.h"
#include <wx/stdpaths.h>
#include <wx/filename.h>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>
#include "ConnectInfo.h"

#if defined(__WXMSW__)
#include <wx/msw/private.h>
#include <Windows.h>
#endif

class MyApp : public wxApp {
public:
    virtual bool OnInit() override;
    virtual int OnRun() override;
};

wxIMPLEMENT_APP(MyApp);

bool MyApp::OnInit() {
    try {
#ifdef __WXMSW__
        // Enable Per-Monitor v2 DPI awareness before creating any windows
        // This tells Windows not to lie about DPI and to report the true physical size
        // Using Windows API directly since wxWidgets SetDpiAwareness may not be available
        typedef HRESULT (WINAPI *SetProcessDpiAwarenessContext_t)(DPI_AWARENESS_CONTEXT);
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        if (user32) {
            SetProcessDpiAwarenessContext_t pSetProcessDpiAwarenessContext = 
                (SetProcessDpiAwarenessContext_t)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
            if (pSetProcessDpiAwarenessContext) {
                pSetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            }
        }
#endif

        wxLog::SetActiveTarget(new wxLogStderr);

        // Set application name for proper config directory
        wxApp::SetAppName("OceanTerm");
        wxApp::SetVendorName("OceanTerm");

        // Initialize global config from command line arguments
        GlobalConfig::InitializeFromCommandLine(argc, argv);

        // Load translations based on system language
        wxLocale locale;
        locale.Init();
        wxString sysLang = locale.GetCanonicalName();
        if (sysLang.StartsWith("zh")) {
            TranslationHelper::Load("zh_CN");
        } else {
            TranslationHelper::Load("en");
        }

        // Master password authentication
        if (!GlobalConfig::HasMasterPassword()) {
            // No master password set, show setup dialog
            MasterPasswordDialog setupDialog(nullptr, true);
            if (setupDialog.ShowModal() != wxID_OK) {
                // User cancelled, exit application
                return false;
            }

            wxString password = setupDialog.GetPassword();
            GlobalConfig::SetMasterPassword(password.ToStdString());

            // Initialize language, font, and font size to system defaults
            wxLocale locale;
            locale.Init();
            wxString sysLang = locale.GetCanonicalName();
            if (sysLang.StartsWith("zh")) {
                GlobalConfig::SetLanguage("zh_CN");
            } else {
                GlobalConfig::SetLanguage("en");
            }

            // Font and font size will use system defaults (empty string and 0)
            GlobalConfig::SetFontName("");
            GlobalConfig::SetFontSize(0);

            GlobalConfig::SaveSettings();
            GlobalConfig::SetActiveMasterPassword(password.ToStdString());
        } else {
            // Master password exists, show login dialog
            int maxAttempts = 3;
            int attempts = 0;

            while (attempts < maxAttempts) {
                MasterPasswordDialog loginDialog(nullptr, false);
                loginDialog.SetPasswordVerifier([&](const wxString& password, wxString& errorMsg, bool& shouldClose) {
                    if (GlobalConfig::VerifyMasterPassword(password.ToStdString())) {
                        return true;
                    } else {
                        attempts++;
                        int newRemaining = maxAttempts - attempts;
                        if (newRemaining > 0) {
                            errorMsg = wxString::Format(TranslationHelper::Tr("incorrectPasswordAttempts"), newRemaining);
                            shouldClose = false;
                        } else {
                            errorMsg = TranslationHelper::Tr("tooManyFailedAttempts");
                            shouldClose = true;
                        }
                        return false;
                    }
                });

                int result = loginDialog.ShowModal();
                if (result == wxID_CANCEL) {
                    // User cancelled or max attempts reached, exit application
                    return false;
                }

                wxString password = loginDialog.GetPassword();
                // Password correct, save to memory and continue
                GlobalConfig::SetActiveMasterPassword(password.ToStdString());
                break;
            }
        }

        // Initialize SSH log file early
        try {
            std::filesystem::path log_dir = std::filesystem::path(GlobalConfig::GetLogPath());
            std::filesystem::create_directories(log_dir);

            auto now = std::chrono::system_clock::now();
            auto t = std::chrono::system_clock::to_time_t(now);
            std::tm tm;
#ifdef _WIN32
            localtime_s(&tm, &t);
#else
            localtime_r(&t, &tm);
#endif
            std::ostringstream filename;
            filename << "ssh_" << std::put_time(&tm, "%Y%m%d_%H%M%S") << ".log";

            std::filesystem::path log_path = log_dir / filename.str();
            ssh_log_file.open(log_path, std::ios::out | std::ios::app);

            if (ssh_log_file.is_open()) {
                ssh_log_initialized = true;
                ssh_log_file << "=== SSH Log Session Started ===" << std::endl;
                ssh_log_file.flush();
                SSH_LOG("Application starting");
            }
        } catch (const std::exception& e) {
            // If log initialization fails, silently continue without logging
        }

        // Get DPI scale factor after enabling DPI awareness and store globally
        double dpiScale = 1.0;
#ifdef __WXMSW__
        HDC hdc = GetDC(nullptr);
        if (hdc) {
            dpiScale = static_cast<double>(GetDeviceCaps(hdc, LOGPIXELSX)) / 96.0;
            ReleaseDC(nullptr, hdc);
        }
#endif
        GlobalConfig::SetDPIScaleFactor(dpiScale);

        // Adjust initial window size by DPI scale factor
        wxSize initialSize(800, 600);
        initialSize.SetWidth(static_cast<int>(initialSize.GetWidth() * dpiScale));
        initialSize.SetHeight(static_cast<int>(initialSize.GetHeight() * dpiScale));

        AppWindow* frame = new AppWindow("OceanTerm", wxPoint(50, 50), initialSize);
        frame->Show(true);
        SSH_LOG("Main window created and shown with DPI scale: " << dpiScale << ", size: " << initialSize.GetWidth() << "x" << initialSize.GetHeight());
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Exception in OnInit: " << e.what() << std::endl;
        return false;
    }
}

int MyApp::OnRun() {
    SSH_LOG("MyApp::OnRun called - entering main loop");
    int result = wxApp::OnRun();
    SSH_LOG("MyApp::OnRun exited with result: " << result);
    return result;
}


wxBEGIN_EVENT_TABLE(AppWindow, wxFrame)
    EVT_MENU(wxID_EXIT, AppWindow::OnQuit)
    EVT_MENU(wxID_NEW, AppWindow::OnNewTab)
    EVT_IDLE(AppWindow::OnIdle)
    EVT_SIZE(AppWindow::OnSize)
    EVT_COMMAND(wxID_ANY, wxEVT_DEVICE_OPEN_REQUEST, AppWindow::OnDeviceOpenRequest)
    EVT_COMMAND(wxID_ANY, wxEVT_DEVICE_DELETE_REQUEST, AppWindow::OnDeviceDeleteRequest)
    EVT_COMMAND(wxID_ANY, wxEVT_DEVICE_LIST_UPDATE, AppWindow::OnDeviceListUpdate)
wxEND_EVENT_TABLE()

AppWindow::AppWindow(const wxString& title, const wxPoint& pos, const wxSize& size)
    : wxFrame(nullptr, wxID_ANY, title, pos, size, wxCLIP_CHILDREN | wxRESIZE_BORDER)
{
    // Initialize locale to fix encoding issues
    static wxLocale locale;
    locale.Init();
    
    // Load translations using TranslationHelper
    std::string lang = GlobalConfig::GetLanguage();
    TranslationHelper::Load(wxString::FromUTF8(lang.c_str()));

    SetDoubleBuffered(true);
    this->SetThemeEnabled(false);
    m_notebook = new wxSimplebook(this, wxID_ANY);
    m_notebook->SetBackgroundColour(wxColour(10, 10, 10));
    m_titleBar = new CustomTitleBar(this, m_notebook, this);

    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(m_titleBar, 0, wxEXPAND);
    sizer->Add(m_notebook, 1, wxEXPAND);

    SetSizer(sizer);
    Layout();

    // Set focus to enable keyboard input
    SetFocus();

    CreateDashboardTab();
    
    Centre();

#ifdef __WXMSW__
    ::SetWindowPos(GetHWND(), NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER);
#endif
}

AppWindow::~AppWindow() {
    // Don't close m_uv_loop - it's shared context for all sockets
    // The loop will be cleaned up when the application exits
}

void AppWindow::CreateDashboardTab() {
    // Create device list panel
    DeviceListPanel* deviceListPanel = new DeviceListPanel(m_notebook);

    // Add tab with device list
    DeviceConfig emptyConfig;
    ConnectInfo* homeTab = m_titleBar->AddTab(TranslationHelper::Tr("home"), deviceListPanel, emptyConfig, false, false);
}

void AppWindow::CreateTerminalTab(const DeviceConfig& device) {
    SSH_LOG("AppWindow::CreateTerminalTab called");
    DeviceConfig newDevice = device;
    SSH_LOG("Device selected: " << newDevice.name);

    TermGLCanvas* terminalCanvas = new TermGLCanvas(m_notebook);
    SSH_LOG("TermGLCanvas created");

    // Create tab label as "username@address"
    wxString tabLabel = wxString::FromUTF8(newDevice.username.c_str()) + "@" + wxString::FromUTF8(newDevice.address.c_str());
    ConnectInfo* newTab = m_titleBar->AddTab(tabLabel, terminalCanvas, newDevice);
    SSH_LOG("Tab added to title bar");

    // Connect to SSH
    if (newTab) {
        SSH_LOG("Calling Connect() on tab");
        newTab->Connect();
        SSH_LOG("Connect() returned");
        
        // Show IME input box by triggering tab selection
        terminalCanvas->ShowIMEInputBox();
        SSH_LOG("IME input box shown for new tab");
    }
}

void AppWindow::OnQuit(wxCommandEvent& WXUNUSED(event)) {
    Close(true);
}

void AppWindow::OnNewTab(wxCommandEvent& WXUNUSED(event)) {
    SSH_LOG("AppWindow::OnNewTab called");
    ConnectionDialog dialog(this, TranslationHelper::Tr("deviceList"));
    if (dialog.ShowModal() == wxID_OK) {
        DeviceConfig newDevice = dialog.GetSelectedDevice();
        SSH_LOG("Device selected: " << newDevice.name);
        CreateTerminalTab(newDevice);
    }
}

void AppWindow::OnDeviceOpenRequest(wxCommandEvent& event) {
    SSH_LOG("AppWindow::OnDeviceOpenRequest called");
    wxString deviceId = event.GetString();
    std::string deviceIdStr = deviceId.ToStdString();

    std::vector<DeviceConfig> devices = DeviceConfig::LoadFromFile();
    auto it = std::find_if(devices.begin(), devices.end(),
        [&deviceIdStr](const DeviceConfig& device) {
            return device.id == deviceIdStr;
        });

    if (it != devices.end()) {
        SSH_LOG("Opening device with id: " << deviceIdStr << ", name: " << it->name);
        CreateTerminalTab(*it);
    } else {
        SSH_LOG("Device not found with id: " << deviceIdStr);
    }
}

void AppWindow::OnDeviceDeleteRequest(wxCommandEvent& event) {
    SSH_LOG("AppWindow::OnDeviceDeleteRequest called");
    wxString deviceId = event.GetString();
    std::string deviceIdStr = deviceId.ToStdString();

    std::vector<DeviceConfig> devices = DeviceConfig::LoadFromFile();
    auto it = std::find_if(devices.begin(), devices.end(),
        [&deviceIdStr](const DeviceConfig& device) {
            return device.id == deviceIdStr;
        });

    if (it != devices.end()) {
        SSH_LOG("Deleting device with id: " << deviceIdStr << ", name: " << it->name);
        devices.erase(it);
        DeviceConfig::SaveToFile(devices);
        // Refresh device list by finding the DeviceListPanel
        for (size_t i = 0; i < m_notebook->GetPageCount(); ++i) {
            wxWindow* page = m_notebook->GetPage(i);
            DeviceListPanel* deviceListPanel = dynamic_cast<DeviceListPanel*>(page);
            if (deviceListPanel) {
                deviceListPanel->DeleteDeviceById(deviceIdStr);
                break;
            }
        }
    } else {
        SSH_LOG("Device not found with id: " << deviceIdStr);
    }
}

void AppWindow::OnDeviceListUpdate(wxCommandEvent& event) {
    SSH_LOG("AppWindow::OnDeviceListUpdate called - refreshing device list");
    // Find the DeviceListPanel and refresh it
    for (size_t i = 0; i < m_notebook->GetPageCount(); ++i) {
        wxWindow* page = m_notebook->GetPage(i);
        DeviceListPanel* deviceListPanel = dynamic_cast<DeviceListPanel*>(page);
        if (deviceListPanel) {
            deviceListPanel->LoadDevices();
            deviceListPanel->RefreshDeviceList();
            break;
        }
    }
}

void AppWindow::OnIdle(wxIdleEvent& event)
{
    static int idle_count = 0;
    idle_count++;
    // No longer need to run libuv loop here - each thread has its own loop
    event.Skip();
}

void AppWindow::OnSize(wxSizeEvent& event) {
    // Force layout recalculation to ensure notebook expands to fill window
    Layout();
    
    // 通知所有tab调整大小
    if (m_titleBar) {
        m_titleBar->NotifyAllTabsResize();
    }
    
    // Log sizes for debugging
    wxSize windowSize = GetClientSize();
    wxSize notebookSize = m_notebook->GetSize();
    SSH_LOG("AppWindow::OnSize - Window: " << windowSize.GetWidth() << "x" << windowSize.GetHeight()
            << ", Notebook: " << notebookSize.GetWidth() << "x" << notebookSize.GetHeight());
    
    event.Skip();
}

#if defined(__WXMSW__)
bool AppWindow::MSWHandleMessage(WXLRESULT* result, WXUINT message, WXWPARAM wParam, WXLPARAM lParam) {
    if (message == WM_NCCALCSIZE) {
        // Remove standard frame to use custom title bar
        *result = 0;
        return true;
    }
    else if (message == WM_GETMINMAXINFO) {
        MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        HMONITOR hMonitor = ::MonitorFromWindow(GetHWND(), MONITOR_DEFAULTTONEAREST);
        if (hMonitor) {
            MONITORINFO mi = { sizeof(MONITORINFO) };
            if (::GetMonitorInfo(hMonitor, &mi)) {
                // Set the maximized size and position to the work area (rcWork)
                // This automatically excludes the taskbar!
                mmi->ptMaxSize.x = mi.rcWork.right - mi.rcWork.left;
                mmi->ptMaxSize.y = mi.rcWork.bottom - mi.rcWork.top;
                mmi->ptMaxPosition.x = mi.rcWork.left - mi.rcMonitor.left;
                mmi->ptMaxPosition.y = mi.rcWork.top - mi.rcMonitor.top;
            }
        }
        *result = 0;
        return true;
    }
    return wxFrame::MSWHandleMessage(result, message, wParam, lParam);
}
#endif
