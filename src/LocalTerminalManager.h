#ifndef LOCALTERMINALMANAGER_H
#define LOCALTERMINALMANAGER_H

#include <string>
#include <vector>

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
    void* m_hPipe;
    void* m_hProcess;
#endif
};

#endif // LOCALTERMINALMANAGER_H
