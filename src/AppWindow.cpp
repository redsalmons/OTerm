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
#include <fstream>
#include "ConnectInfo.h"
#include "TerminalPanel.h"
#include "InfiniteSplitter.h"
#include "LocalTerminalContainer.h"

#if defined(__WXMSW__)
#include <wx/msw/private.h>
#include <Windows.h>
#endif

// Custom logger that writes to both stderr and a fixed log file
class wxLogFileAndStderr : public wxLog {
public:
    wxLogFileAndStderr(const std::string& path) : m_path(path) {
        std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    }
protected:
    virtual void DoLogText(const wxString& msg) override {
        wxLogStderr* stderrLog = new wxLogStderr();
        stderrLog->LogText(msg);
        delete stderrLog;
        std::ofstream f(m_path, std::ios::app);
        if (f.is_open()) {
            auto now = std::chrono::system_clock::now();
            auto t = std::chrono::system_clock::to_time_t(now);
            std::tm tm;
#ifdef _WIN32
            localtime_s(&tm, &t);
#else
            localtime_r(&t, &tm);
#endif
            f << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << " [wxLog] " << msg.ToStdString() << std::endl;
        }
    }
private:
    std::string m_path;
};

static void CustomAssertHandler(const wxString& file, int line, const wxString& func,
                                const wxString& cond, const wxString& msg) {
    std::ofstream f((std::filesystem::temp_directory_path() / "oterm_alert.log").string(), std::ios::app);
    if (f.is_open()) {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        std::tm tm;
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        f << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << " [ASSERT] ";
        f << "File: " << file.ToStdString() << ":" << line;
        if (!func.IsEmpty()) f << " Func: " << func.ToStdString();
        if (!cond.IsEmpty()) f << " Condition: " << cond.ToStdString();
        if (!msg.IsEmpty()) f << " Message: " << msg.ToStdString();
        f << std::endl;
    }
    // Also write to stderr so it shows up in console if attached
    std::cerr << "ASSERT in " << file.ToStdString() << ":" << line;
    if (!cond.IsEmpty()) std::cerr << " condition '" << cond.ToStdString() << "'";
    if (!msg.IsEmpty()) std::cerr << " message '" << msg.ToStdString() << "'";
    std::cerr << std::endl;
}

class MyApp : public wxApp {
public:
    virtual bool OnInit() override;
    virtual int OnRun() override;
    virtual void OnAssertFailure(const wxChar* file, int line, const wxChar* func,
                                 const wxChar* cond, const wxChar* msg) override;
};

wxIMPLEMENT_APP(MyApp);

void MyApp::OnAssertFailure(const wxChar* file, int line, const wxChar* func,
                            const wxChar* cond, const wxChar* msg) {
    std::ofstream f((std::filesystem::temp_directory_path() / "oterm_alert.log").string(), std::ios::app);
    if (f.is_open()) {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        std::tm tm;
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        f << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << " [ASSERT] ";
        if (file) f << "File: " << wxString(file).ToStdString() << ":" << line;
        if (func && wxString(func).Len() > 0) f << " Func: " << wxString(func).ToStdString();
        if (cond && wxString(cond).Len() > 0) f << " Condition: " << wxString(cond).ToStdString();
        if (msg && wxString(msg).Len() > 0) f << " Message: " << wxString(msg).ToStdString();
        f << std::endl;
    }
    std::cerr << "ASSERT in " << (file ? wxString(file).ToStdString() : "?") << ":" << line;
    if (cond) std::cerr << " condition '" << wxString(cond).ToStdString() << "'";
    if (msg) std::cerr << " message '" << wxString(msg).ToStdString() << "'";
    std::cerr << std::endl;
}

bool MyApp::OnInit() {
    std::ofstream f((std::filesystem::temp_directory_path() / "oterm_alert.log").string(), std::ios::app);
    if (f.is_open()) f << "[APP] MyApp::OnInit called" << std::endl;

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

        wxLog::SetActiveTarget(new wxLogFileAndStderr((std::filesystem::temp_directory_path() / "oterm_wx.log").string()));

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


wxDEFINE_EVENT(wxEVT_SSH_DIRECT_CONNECT, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_DEVICE_SHOW_REQUEST, wxCommandEvent);

wxBEGIN_EVENT_TABLE(AppWindow, wxFrame)
    EVT_MENU(wxID_EXIT, AppWindow::OnQuit)
    EVT_CLOSE(AppWindow::OnClose)
    EVT_MENU(wxID_NEW, AppWindow::OnNewTab)
    EVT_MENU(ID_NEW_TAB, AppWindow::OnNewLocalTerminal)
    EVT_IDLE(AppWindow::OnIdle)
    EVT_SIZE(AppWindow::OnSize)
    EVT_COMMAND(wxID_ANY, wxEVT_DEVICE_OPEN_REQUEST, AppWindow::OnDeviceOpenRequest)
    EVT_COMMAND(wxID_ANY, wxEVT_DEVICE_DELETE_REQUEST, AppWindow::OnDeviceDeleteRequest)
    EVT_COMMAND(wxID_ANY, wxEVT_DEVICE_LIST_UPDATE, AppWindow::OnDeviceListUpdate)
    EVT_COMMAND(wxID_ANY, wxEVT_SSH_DIRECT_CONNECT, AppWindow::OnSSHDirectConnect)
    EVT_COMMAND(wxID_ANY, wxEVT_DEVICE_SHOW_REQUEST, AppWindow::OnDeviceShowRequest)
    EVT_MENU(wxID_ANY, AppWindow::OnFileTransferRequest)
wxEND_EVENT_TABLE()

AppWindow::AppWindow(const wxString& title, const wxPoint& pos, const wxSize& size)
    : wxFrame(nullptr, wxID_ANY, title, pos, size, wxCLIP_CHILDREN | wxRESIZE_BORDER)
{
    std::ofstream f((std::filesystem::temp_directory_path() / "oterm_alert.log").string(), std::ios::app);
    if (f.is_open()) f << "[APPWINDOW] AppWindow constructor called" << std::endl;

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

    // CreateLocalTerminalTab directly instead of starting with a homepage dashboard
    CreateLocalTerminalTab();

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

    // Create TerminalPanel without LocalTerminalContainer (will use SSH thread)
    TerminalPanel* terminalPanel = new TerminalPanel(m_notebook, nullptr);
    
    // Create TermGLCanvas and set it to the panel
    TermGLCanvas* terminalCanvas = new TermGLCanvas(terminalPanel, false);
    terminalPanel->SetCanvas(terminalCanvas);
    
    // Create tab label as "username@address"
    wxString tabLabel = wxString::FromUTF8(newDevice.username.c_str()) + "@" + wxString::FromUTF8(newDevice.address.c_str());
    ConnectInfo* newTab = m_titleBar->AddTab(tabLabel, terminalPanel, newDevice);
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

void AppWindow::CreateLocalTerminalTab() {
    {
        std::ofstream f((std::filesystem::temp_directory_path() / "oterm_alert.log").string(), std::ios::app);
        if (f.is_open()) f << "[APP] CreateLocalTerminalTab entered" << std::endl;
    }

    // Create TerminalPanel with LocalTerminalContainer
    auto container = std::make_unique<LocalTerminalContainer>(24, 120, "");
    {
        std::ofstream f((std::filesystem::temp_directory_path() / "oterm_alert.log").string(), std::ios::app);
        if (f.is_open()) f << "[APP] LocalTerminalContainer created, thread=" << (container->GetThread() ? "yes" : "no") << std::endl;
    }
    
    TerminalPanel* terminalPanel = new TerminalPanel(m_notebook, std::move(container));
    {
        std::ofstream f((std::filesystem::temp_directory_path() / "oterm_alert.log").string(), std::ios::app);
        if (f.is_open()) f << "[APP] TerminalPanel created, IsLocalTerminal=" << terminalPanel->IsLocalTerminal() << std::endl;
    }
    
    // Create TermGLCanvas and set it to the panel
    TermGLCanvas* terminalCanvas = new TermGLCanvas(terminalPanel, false);
    terminalPanel->SetCanvas(terminalCanvas);
    {
        std::ofstream f((std::filesystem::temp_directory_path() / "oterm_alert.log").string(), std::ios::app);
        if (f.is_open()) f << "[APP] TermGLCanvas created and set" << std::endl;
    }
    
    // Note: key callback is set by TerminalPanel::SetCanvas -> SetupCanvasConnection
    
    DeviceConfig emptyConfig;
    wxString tabLabel = TranslationHelper::Tr("localTerminal");
    if (tabLabel.IsEmpty()) {
        tabLabel = "Local";
    }
    ConnectInfo* newTab = m_titleBar->AddTab(tabLabel, terminalPanel, emptyConfig, true, true);

    {
        std::ofstream f((std::filesystem::temp_directory_path() / "oterm_alert.log").string(), std::ios::app);
        if (f.is_open()) f << "[APP] AddTab returned newTab=" << (newTab ? "yes" : "no") << std::endl;
    }

    if (newTab) {
        SSH_LOG("Calling Connect() on tab");
        newTab->Connect();
        SSH_LOG("Connect() returned");
        CallAfter([terminalCanvas]() {
            if (terminalCanvas) {
                terminalCanvas->SetFocus();
                terminalCanvas->ShowIMEInputBox();
            }
        });
    } else {
        SSH_LOG("ERROR: newTab is null");
    }
}

void AppWindow::OnQuit(wxCommandEvent& WXUNUSED(event)) {
    Close(true);
}

void AppWindow::OnClose(wxCloseEvent& event) {
    SSH_LOG("AppWindow::OnClose called");
    
    // Stop all terminal threads before closing
    // Iterate through all tabs and stop their threads
    if (m_notebook) {
        for (size_t i = 0; i < m_notebook->GetPageCount(); ++i) {
            wxWindow* page = m_notebook->GetPage(i);
            if (page) {
                // Try to find TermGLCanvas and stop its threads
                TermGLCanvas* canvas = dynamic_cast<TermGLCanvas*>(page);
                if (!canvas) {
                    // Check if it's a TerminalPanel
                    TerminalPanel* panel = dynamic_cast<TerminalPanel*>(page);
                    if (panel) {
                        canvas = panel->GetCanvas();
                    }
                }
                if (!canvas) {
                    // Check if it's an InfiniteSplitter
                    InfiniteSplitter* splitter = dynamic_cast<InfiniteSplitter*>(page);
                    if (splitter) {
                        wxWindow* window1 = splitter->GetWindow1();
                        if (window1) {
                            TerminalPanel* panel = dynamic_cast<TerminalPanel*>(window1);
                            if (panel) canvas = panel->GetCanvas();
                        }
                        if (!canvas && splitter->GetWindow2()) {
                            wxWindow* window2 = splitter->GetWindow2();
                            if (window2) {
                                TerminalPanel* panel = dynamic_cast<TerminalPanel*>(window2);
                                if (panel) canvas = panel->GetCanvas();
                            }
                        }
                    }
                }
                
                if (canvas) {
                    SSH_LOG("Stopping threads for canvas at page " << i);
                    canvas->StopThreads();
                }
            }
        }
    }
    
    SSH_LOG("All threads stopped, proceeding with close");
    event.Skip();
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

void AppWindow::OnNewLocalTerminal(wxCommandEvent& WXUNUSED(event)) {
    SSH_LOG("AppWindow::OnNewLocalTerminal called");
    CreateLocalTerminalTab();
}

void AppWindow::OnSSHDirectConnect(wxCommandEvent& event) {
    SSH_LOG("AppWindow::OnSSHDirectConnect called");
    TerminalPanel* panel = (TerminalPanel*)event.GetEventObject();
    if (!panel) {
        SSH_LOG("OnSSHDirectConnect: panel is null");
        return;
    }
    
    wxString cmd = event.GetString();
    std::string cmdStr = cmd.ToStdString();
    SSH_LOG("SSH direct connect command: " << cmdStr);

    // Parse "oc ssh [user@]address[:port]"
    // Remove "oc ssh" prefix
    std::string target = cmdStr;
    size_t sshPos = target.find("oc ssh");
    if (sshPos == 0) {
        target = target.substr(7); // skip "oc ssh"
    }
    // Trim whitespace
    size_t start = target.find_first_not_of(" \t");
    if (start == std::string::npos) {
        SSH_LOG("No target specified after oc ssh");
        return;
    }
    target = target.substr(start);

    // Extract port if present (address:port)
    std::string port = "22";
    size_t colonPos = target.rfind(':');
    if (colonPos != std::string::npos) {
        port = target.substr(colonPos + 1);
        target = target.substr(0, colonPos);
    }

    // Extract username if present (user@address)
    std::string username = "root";
    std::string address = target;
    size_t atPos = target.find('@');
    if (atPos != std::string::npos) {
        username = target.substr(0, atPos);
        address = target.substr(atPos + 1);
    }

    SSH_LOG("Parsed SSH: username=" << username << ", address=" << address << ", port=" << port);

    // Validate that we have an address
    if (address.empty()) {
        SSH_LOG("No address specified for SSH connection");
        // Don't return, just log and continue - the SSH connection will fail gracefully
        // This allows the terminal to remain functional
        return;
    }

    DeviceConfig device;
    device.id = "direct_" + username + "@" + address;
    device.name = username + "@" + address;
    device.username = username;
    device.address = address;
    device.port = port;
    device.auth_method = "password";

    // Convert the current local terminal panel to SSH
    panel->ConvertToSSH(device);
    
    // Update tab label to show the device
    wxString newLabel = wxString::FromUTF8(device.username.c_str()) + "@" + wxString::FromUTF8(device.address.c_str());
    // Find the ConnectInfo for this panel and update its label
    for (size_t i = 0; i < m_notebook->GetPageCount(); ++i) {
        wxWindow* page = m_notebook->GetPage(i);
        if (page == panel) {
            // Update the corresponding tab label
            // This would require finding the ConnectInfo, but for now just log
            SSH_LOG("Would update tab label to: " << newLabel.ToStdString());
            break;
        }
    }
}

void AppWindow::OnDeviceShowRequest(wxCommandEvent& event) {
    SSH_LOG("AppWindow::OnDeviceShowRequest called");
    TerminalPanel* panel = (TerminalPanel*)event.GetEventObject();
    if (!panel) {
        SSH_LOG("OnDeviceShowRequest: panel is null");
        return;
    }

    ConnectionDialog dialog(this, TranslationHelper::Tr("deviceList"));
    if (dialog.ShowModal() == wxID_OK) {
        DeviceConfig newDevice = dialog.GetSelectedDevice();
        SSH_LOG("Device selected: " << newDevice.name);
        
        // Convert the current local terminal panel to SSH
        panel->ConvertToSSH(newDevice);
        
        // Find the ConnectInfo for this panel, update its label and internal thread pointers
        if (m_titleBar) {
            for (auto* tab : m_titleBar->GetTabs()) {
                if (tab->GetContentPanel() == panel) {
                    SSH_LOG("OnDeviceShowRequest: found ConnectInfo tab=" << tab << ", switching to SSH");
                    tab->SwitchToSSH(panel->GetSSHThread(), newDevice);
                    
                    // Notify layout update to adjust tab sizes
                    wxCommandEvent layoutEvent(wxEVT_COMMAND_MENU_SELECTED, wxID_ANY);
                    layoutEvent.SetInt(1); // Flag to indicate tab label changed
                    wxPostEvent(this, layoutEvent);
                    break;
                }
            }
        }
    } else {
        SSH_LOG("Device selection cancelled");
    }
}

void AppWindow::OnFileTransferRequest(wxCommandEvent& event) {
    SSH_LOG("AppWindow::OnFileTransferRequest called, GetInt()=" << event.GetInt());
    if (event.GetInt() == 2) {
        wxWindow* sender = dynamic_cast<wxWindow*>(event.GetEventObject());
        if (sender) {
            // Walk up to find TerminalPanel
            TerminalPanel* panel = nullptr;
            wxWindow* curr = sender;
            while (curr) {
                panel = dynamic_cast<TerminalPanel*>(curr);
                if (panel) break;
                curr = curr->GetParent();
            }
            
            if (panel) {
                SSH_LOG("AppWindow::OnFileTransferRequest: found TerminalPanel=" << panel);
                // Find the top-level notebook page that contains this panel
                wxWindow* page = panel;
                while (page && page->GetParent() != m_notebook) {
                    page = page->GetParent();
                }
                
                if (page && m_notebook && m_titleBar) {
                    int pageIndex = m_notebook->FindPage(page);
                    SSH_LOG("AppWindow::OnFileTransferRequest: found notebook page index=" << pageIndex);
                    if (pageIndex != wxNOT_FOUND && pageIndex < (int)m_titleBar->GetTabs().size()) {
                        ConnectInfo* tab = m_titleBar->GetTabs()[pageIndex];
                        SSH_LOG("AppWindow::OnFileTransferRequest: found ConnectInfo=" << tab << ", showing dialog");
                        tab->ShowFileTransferDialog();
                        return;
                    }
                }
                SSH_LOG("AppWindow::OnFileTransferRequest: ConnectInfo not found for panel=" << panel);
            } else {
                SSH_LOG("AppWindow::OnFileTransferRequest: TerminalPanel not found in sender hierarchy");
            }
        }
    } else {
        event.Skip();
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
