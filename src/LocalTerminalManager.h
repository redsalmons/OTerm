#ifndef LOCALTERMINALMANAGER_H
#define LOCALTERMINALMANAGER_H

#include <string>
#include <vector>

#ifdef _WIN32
typedef int pid_t;
#endif

class LocalTerminalManager {
public:
    LocalTerminalManager();
    ~LocalTerminalManager();

    bool Start(const std::string& shell = "");
    void Stop();
    bool Write(const char* data, size_t len);
    int Read(char* buffer, size_t len);
    void Resize(int rows, int cols);
    bool IsRunning() const { return m_running; }

private:
    int m_masterFd;
    int m_slaveFd;
    pid_t m_childPid;
    bool m_running;

#ifdef _WIN32
    void* m_hConPTY;        // HPCON pseudo console handle
    void* m_hInPipeWrite;   // App writes user keystrokes here (PTY stdin)
    void* m_hOutPipeRead;   // App reads PTY output from here (PTY stdout/stderr)
    void* m_hProcess;       // Shell process handle
    void* m_hThread;       // Shell process main thread handle
#endif
};

#endif // LOCALTERMINALMANAGER_H
