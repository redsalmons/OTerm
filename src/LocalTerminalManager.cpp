#include "LocalTerminalManager.h"
#include "SSHManager.h"
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>
#include <iomanip>

#ifdef __APPLE__
#include <util.h>
#include <sys/ioctl.h>
#elif defined(__linux__)
#include <pty.h>
#include <sys/ioctl.h>
#elif defined(_WIN32)
#include <windows.h>
#include <conpty.h>
#include <stdbool.h>
#endif

LocalTerminalManager::LocalTerminalManager()
    : m_masterFd(-1), m_slaveFd(-1), m_childPid(-1), m_running(false)
#ifdef _WIN32
    , m_hPipe(nullptr), m_hProcess(nullptr)
#endif
{
}

LocalTerminalManager::~LocalTerminalManager() {
    Stop();
}

bool LocalTerminalManager::Start(const std::string& shell) {
#ifdef _WIN32
    // Windows ConPTY implementation
    HRESULT hr;
    HPCON hPseudoConsole = nullptr;
    HANDLE hPipeRead = nullptr, hPipeWrite = nullptr;

    // Create pipes for communication
    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
    if (!CreatePipe(&hPipeRead, &hPipeWrite, &sa, 0)) {
        return false;
    }

    // Set the read end to non-blocking
    DWORD mode = PIPE_NOWAIT;
    SetNamedPipeHandleState(hPipeRead, &mode, nullptr, nullptr);

    // Create pseudo console
    COORD size = { 80, 25 };
    hr = CreatePseudoConsole(size, hPipeRead, hPipeWrite, 0, &hPseudoConsole);
    if (FAILED(hr)) {
        CloseHandle(hPipeRead);
        CloseHandle(hPipeWrite);
        return false;
    }

    // Create startup info for the process
    STARTUPINFOEXW siEx = { 0 };
    siEx.StartupInfo.cb = sizeof(siEx);
    SIZE_T bytesRequired = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &bytesRequired);
    siEx.lpAttributeList = (PPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(GetProcessHeap(), 0, bytesRequired);
    InitializeProcThreadAttributeList(siEx.lpAttributeList, 1, 0, &bytesRequired);
    UpdateProcThreadAttribute(siEx.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, hPseudoConsole, sizeof(hPseudoConsole), nullptr, nullptr);

    // Get default shell
    std::wstring shellPath = L"cmd.exe";
    if (!shell.empty()) {
        int needed = MultiByteToWideChar(CP_UTF8, 0, shell.c_str(), -1, nullptr, 0);
        shellPath.resize(needed);
        MultiByteToWideChar(CP_UTF8, 0, shell.c_str(), -1, &shellPath[0], needed);
    }

    // Create the process
    PROCESS_INFORMATION pi = { 0 };
    if (!CreateProcessW(nullptr, &shellPath[0], nullptr, nullptr, FALSE, 
                        CREATE_UNICODE_ENVIRONMENT, nullptr, nullptr, 
                        &siEx.StartupInfo, &pi)) {
        DeleteProcThreadAttributeList(siEx.lpAttributeList);
        HeapFree(GetProcessHeap(), 0, siEx.lpAttributeList);
        ClosePseudoConsole(hPseudoConsole);
        CloseHandle(hPipeRead);
        CloseHandle(hPipeWrite);
        return false;
    }

    // Clean up
    DeleteProcThreadAttributeList(siEx.lpAttributeList);
    HeapFree(GetProcessHeap(), 0, siEx.lpAttributeList);
    CloseHandle(pi.hThread);

    m_hPipe = hPipeWrite;
    m_hProcess = pi.hProcess;
    m_running = true;
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

        // Execute shell
        execl(actualShell.c_str(), actualShell.c_str(), nullptr);
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
    if (m_hProcess) {
        TerminateProcess((HANDLE)m_hProcess, 0);
        CloseHandle((HANDLE)m_hProcess);
        m_hProcess = nullptr;
    }
    if (m_hPipe) {
        CloseHandle((HANDLE)m_hPipe);
        m_hPipe = nullptr;
    }
#elif defined(__APPLE__) || defined(__linux__)
    if (m_childPid > 0) {
        kill(m_childPid, SIGTERM);
        waitpid(m_childPid, nullptr, 0);
        m_childPid = -1;
    }
    if (m_masterFd >= 0) {
        close(m_masterFd);
        m_masterFd = -1;
    }
    if (m_slaveFd >= 0) {
        close(m_slaveFd);
        m_slaveFd = -1;
    }
#endif

    m_running = false;
}

bool LocalTerminalManager::Write(const char* data, size_t len) {
    if (!m_running) return false;

    std::stringstream ss;
    ss << "LocalTerminalManager::Write: len=" << len << ", content=";
    for (size_t i = 0; i < len && i < 3; i++) {
        ss << std::hex << (int)(unsigned char)data[i] << " ";
    }
    ss << std::dec;
    SSH_LOG(ss.str());

#ifdef _WIN32
    DWORD written = 0;
    return WriteFile((HANDLE)m_hPipe, data, len, &written, nullptr) && written == len;
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
    DWORD read = 0;
    if (ReadFile((HANDLE)m_hPipe, buffer, len, &read, nullptr)) {
        return read;
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
    // ConPTY resize would require recreating the pseudo console
    // For now, this is a limitation
#elif defined(__APPLE__) || defined(__linux__)
    struct winsize ws = { (unsigned short)rows, (unsigned short)cols, 0, 0 };
    ioctl(m_masterFd, TIOCSWINSZ, &ws);
#endif
}
