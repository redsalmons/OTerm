#include "LocalTerminalManager.h"
#include "SSHManager.h"
#include <cstring>
#include <iomanip>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
// ConPTY is not available on older Windows versions
// #include <conpty.h>
#include <stdbool.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>
#endif

#ifdef __APPLE__
#include <util.h>
#include <sys/ioctl.h>
#elif defined(__linux__)
#include <pty.h>
#include <sys/ioctl.h>
#endif

#include <fstream>

static void LT_LOG(const std::string& msg) {
    (void)msg; // Suppress unused parameter warning
}

LocalTerminalManager::LocalTerminalManager()
    : m_masterFd(-1), m_slaveFd(-1), m_childPid(-1), m_running(false)
#ifdef _WIN32
    , m_hConPTY(nullptr), m_hInPipeWrite(nullptr), m_hOutPipeRead(nullptr),
      m_hProcess(nullptr), m_hThread(nullptr)
#endif
{
}

LocalTerminalManager::~LocalTerminalManager() {
    Stop();
}

bool LocalTerminalManager::Start(const std::string& shell) {
#ifdef _WIN32
    // Windows ConPTY implementation (requires Windows 10 1809+)
    LT_LOG("LocalTerminalManager::Start() called, shell='" + shell + "'");
    HRESULT hr;
    HPCON hPC = nullptr;
    HANDLE hInPipeRead = nullptr, hInPipeWrite = nullptr;
    HANDLE hOutPipeRead = nullptr, hOutPipeWrite = nullptr;

    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };

    // Create input pipe: App writes -> PTY reads (stdin)
    if (!CreatePipe(&hInPipeRead, &hInPipeWrite, &sa, 0)) {
        LT_LOG("LocalTerminalManager::Start() CreatePipe(input) failed, err=" + std::to_string(GetLastError()));
        return false;
    }
    LT_LOG("LocalTerminalManager::Start() input pipe created");

    // Create output pipe: PTY writes -> App reads (stdout/stderr)
    if (!CreatePipe(&hOutPipeRead, &hOutPipeWrite, &sa, 0)) {
        CloseHandle(hInPipeRead);
        CloseHandle(hInPipeWrite);
        return false;
    }

    // Create pseudo console.
    // hInPipeRead  = PTY reads user input from here
    // hOutPipeWrite = PTY writes terminal output here
    COORD size = { 80, 25 };
    hr = CreatePseudoConsole(size, hInPipeRead, hOutPipeWrite, 0, &hPC);
    if (FAILED(hr)) {
        LT_LOG("LocalTerminalManager::Start() CreatePseudoConsole failed, hr=" + std::to_string(hr));
        CloseHandle(hInPipeRead);
        CloseHandle(hInPipeWrite);
        CloseHandle(hOutPipeRead);
        CloseHandle(hOutPipeWrite);
        return false;
    }
    LT_LOG("LocalTerminalManager::Start() CreatePseudoConsole succeeded");

    // ConPTY duplicates the handles internally; close PTY-side ends on our side.
    CloseHandle(hInPipeRead);
    hInPipeRead = nullptr;
    CloseHandle(hOutPipeWrite);
    hOutPipeWrite = nullptr;

    // Prepare STARTUPINFOEXW with PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
    STARTUPINFOEXW siEx = {};
    siEx.StartupInfo.cb = sizeof(siEx);
    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
    siEx.lpAttributeList = (PPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(GetProcessHeap(), 0, attrSize);
    if (!siEx.lpAttributeList) {
        LT_LOG("LocalTerminalManager::Start() HeapAlloc for attribute list failed");
        ClosePseudoConsole(hPC);
        CloseHandle(hInPipeWrite);
        CloseHandle(hOutPipeRead);
        return false;
    }
    if (!InitializeProcThreadAttributeList(siEx.lpAttributeList, 1, 0, &attrSize)) {
        LT_LOG("LocalTerminalManager::Start() InitializeProcThreadAttributeList failed, err=" + std::to_string(GetLastError()));
        HeapFree(GetProcessHeap(), 0, siEx.lpAttributeList);
        ClosePseudoConsole(hPC);
        CloseHandle(hInPipeWrite);
        CloseHandle(hOutPipeRead);
        return false;
    }
    if (!UpdateProcThreadAttribute(siEx.lpAttributeList, 0,
                              PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                              hPC, sizeof(hPC), nullptr, nullptr)) {
        LT_LOG("LocalTerminalManager::Start() UpdateProcThreadAttribute failed, err=" + std::to_string(GetLastError()));
        DeleteProcThreadAttributeList(siEx.lpAttributeList);
        HeapFree(GetProcessHeap(), 0, siEx.lpAttributeList);
        ClosePseudoConsole(hPC);
        CloseHandle(hInPipeWrite);
        CloseHandle(hOutPipeRead);
        return false;
    }

    // Choose shell executable
    std::wstring shellCmd;
    if (!shell.empty()) {
        int needed = MultiByteToWideChar(CP_UTF8, 0, shell.c_str(), -1, nullptr, 0);
        shellCmd.resize(needed);
        MultiByteToWideChar(CP_UTF8, 0, shell.c_str(), -1, &shellCmd[0], needed);
    } else {
        // Default to PowerShell if available, otherwise cmd.exe
        wchar_t psPath[MAX_PATH];
        DWORD psLen = ExpandEnvironmentStringsW(L"%SystemRoot%\\System32\\WindowsPowerShell\\v1.0\\powershell.exe", psPath, MAX_PATH);
        if (psLen > 0 && psLen < MAX_PATH && GetFileAttributesW(psPath) != INVALID_FILE_ATTRIBUTES) {
            shellCmd = psPath;
        } else {
            shellCmd = L"cmd.exe";
        }
    }

    // Build command line: shell + interactive/login flags for better terminal behavior
    std::wstring cmdLine = shellCmd;
    if (cmdLine.find(L"powershell.exe") != std::wstring::npos ||
        cmdLine.find(L"pwsh.exe") != std::wstring::npos) {
        cmdLine += L" -NoExit -Command \"$host.UI.RawUI.WindowTitle='OceanTerm Local'\"";
    }

    // Create the child process attached to the pseudo console
    PROCESS_INFORMATION pi = {};
    BOOL created = CreateProcessW(
        nullptr, &cmdLine[0], nullptr, nullptr, FALSE,
        EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT,
        nullptr, nullptr, &siEx.StartupInfo, &pi);

    // Cleanup attribute list regardless of result
    DeleteProcThreadAttributeList(siEx.lpAttributeList);
    HeapFree(GetProcessHeap(), 0, siEx.lpAttributeList);

    if (!created) {
        LT_LOG("LocalTerminalManager::Start() CreateProcessW failed, err=" + std::to_string(GetLastError()));
        ClosePseudoConsole(hPC);
        CloseHandle(hInPipeWrite);
        CloseHandle(hOutPipeRead);
        return false;
    }
    LT_LOG("LocalTerminalManager::Start() CreateProcessW succeeded");

    CloseHandle(pi.hThread); // We don't need the thread handle

    m_hConPTY = hPC;
    m_hInPipeWrite = hInPipeWrite;
    m_hOutPipeRead = hOutPipeRead;
    m_hProcess = pi.hProcess;
    m_running = true;
    LT_LOG("LocalTerminalManager::Start() success");
    return true;

#elif defined(__APPLE__) || defined(__linux__)
    // macOS/Linux PTY implementation
    std::string actualShell = shell;
    if (actualShell.empty()) {
        actualShell = getenv("SHELL") ? getenv("SHELL") : "/bin/bash";
    }

    // Open PTY
#if defined(__APPLE__)
    if (openpty(&m_masterFd, &m_slaveFd, nullptr, nullptr, nullptr) == -1) {
        return false;
    }
#elif defined(__linux__)
    if (openpty(&m_masterFd, &m_slaveFd, nullptr, nullptr, nullptr) == -1) {
        return false;
    }
#endif

    // Set master to non-blocking
    int flags = fcntl(m_masterFd, F_GETFL, 0);
    fcntl(m_masterFd, F_SETFL, flags | O_NONBLOCK);

    // Fork and exec
    m_childPid = fork();
    if (m_childPid == -1) {
        close(m_masterFd);
        close(m_slaveFd);
        return false;
    }

    if (m_childPid == 0) {
        // Child process
        setsid();
        ioctl(m_slaveFd, TIOCSCTTY, nullptr);

        // Close master in child
        close(m_masterFd);

        // Duplicate slave to stdin/stdout/stderr
        dup2(m_slaveFd, STDIN_FILENO);
        dup2(m_slaveFd, STDOUT_FILENO);
        dup2(m_slaveFd, STDERR_FILENO);
        close(m_slaveFd);

        // Set TERM environment variable
        setenv("TERM", "xterm-256color", 1);

        // Execute shell as a login shell (argv[0] starting with '-') so profiles like .zshrc are loaded
        std::string shellName = actualShell;
        size_t lastSlash = shellName.find_last_of('/');
        if (lastSlash != std::string::npos) {
            shellName = shellName.substr(lastSlash + 1);
        }
        std::string loginShellName = "-" + shellName;

        execl(actualShell.c_str(), loginShellName.c_str(), nullptr);
        _exit(1);
    }

    // Close slave in parent
    close(m_slaveFd);
    m_slaveFd = -1;
    m_running = true;
    return true;
#else
    return false;
#endif
}

void LocalTerminalManager::Stop() {
    if (!m_running) return;

#ifdef _WIN32
    // Close input pipe first so the shell sees EOF and can exit gracefully
    if (m_hInPipeWrite) {
        CloseHandle((HANDLE)m_hInPipeWrite);
        m_hInPipeWrite = nullptr;
    }

    // Give the process a brief chance to exit on its own
    if (m_hProcess) {
        WaitForSingleObject((HANDLE)m_hProcess, 500);
        TerminateProcess((HANDLE)m_hProcess, 0);
        WaitForSingleObject((HANDLE)m_hProcess, 1000);
        CloseHandle((HANDLE)m_hProcess);
        m_hProcess = nullptr;
    }

    // Close output pipe
    if (m_hOutPipeRead) {
        CloseHandle((HANDLE)m_hOutPipeRead);
        m_hOutPipeRead = nullptr;
    }

    // Close pseudo console last
    if (m_hConPTY) {
        ClosePseudoConsole((HPCON)m_hConPTY);
        m_hConPTY = nullptr;
    }
#elif defined(__APPLE__) || defined(__linux__)
    // Close master and slave first so child processes see EOF and SIGHUP and exit gracefully
    if (m_masterFd >= 0) {
        close(m_masterFd);
        m_masterFd = -1;
    }
    if (m_slaveFd >= 0) {
        close(m_slaveFd);
        m_slaveFd = -1;
    }

    if (m_childPid > 0) {
        kill(m_childPid, SIGTERM);
        
        // Wait with a short timeout using WNOHANG to prevent blocking the UI thread
        int status = 0;
        pid_t res = waitpid(m_childPid, &status, WNOHANG);
        if (res == 0) {
            // Give it 50ms to exit gracefully
            usleep(50000);
            res = waitpid(m_childPid, &status, WNOHANG);
            if (res == 0) {
                // If still running, force kill
                kill(m_childPid, SIGKILL);
                waitpid(m_childPid, &status, 0);
            }
        }
        m_childPid = -1;
    }
#endif

    m_running = false;
}

bool LocalTerminalManager::Write(const char* data, size_t len) {
    if (!m_running) {
        LT_LOG("Write: not running, rejected");
        return false;
    }
    LT_LOG("Write: len=" + std::to_string(len));

#ifdef _WIN32
    if (!m_hInPipeWrite) return false;
    DWORD written = 0;
    BOOL ok = WriteFile((HANDLE)m_hInPipeWrite, data, (DWORD)len, &written, nullptr);
    return ok && written == len;
#elif defined(__APPLE__) || defined(__linux__)
    ssize_t result = write(m_masterFd, data, len);
    std::stringstream ss2;
    ss2 << "LocalTerminalManager::Write: write result=" << result;
    SSH_LOG(ss2.str());
    return result == (ssize_t)len;
#else
    return false;
#endif
}

int LocalTerminalManager::Read(char* buffer, size_t len) {
    if (!m_running) return -1;

#ifdef _WIN32
    if (!m_hOutPipeRead) return -1;

    // Use PeekNamedPipe to avoid blocking when no data is available.
    DWORD avail = 0;
    if (!PeekNamedPipe((HANDLE)m_hOutPipeRead, nullptr, 0, nullptr, &avail, nullptr)) {
        return -1; // Pipe broken
    }
    if (avail == 0) {
        return 0; // No data right now
    }

    DWORD toRead = (avail < (DWORD)len) ? avail : (DWORD)len;
    DWORD read = 0;
    if (ReadFile((HANDLE)m_hOutPipeRead, buffer, toRead, &read, nullptr)) {
        return (int)read;
    }
    return -1;
#elif defined(__APPLE__) || defined(__linux__)
    ssize_t result = read(m_masterFd, buffer, len);
    if (result < 0 && errno == EAGAIN) {
        return 0;
    }
    return result;
#else
    return -1;
#endif
}

void LocalTerminalManager::Resize(int rows, int cols) {
    if (!m_running) return;

#ifdef _WIN32
    if (m_hConPTY) {
        COORD size = { (SHORT)cols, (SHORT)rows };
        ResizePseudoConsole((HPCON)m_hConPTY, size);
    }
#elif defined(__APPLE__) || defined(__linux__)
    struct winsize ws = { (unsigned short)rows, (unsigned short)cols, 0, 0 };
    ioctl(m_masterFd, TIOCSWINSZ, &ws);
#endif
}
