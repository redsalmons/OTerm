#include "SSHManager.h"
#include "GlobalConfig.h"

// External variables for logging
std::ofstream ssh_log_file;
bool ssh_log_initialized = false;

void InitSSHLogFile() {
    if (ssh_log_initialized) {
        return;
    }

    try {
        std::filesystem::path log_dir = std::filesystem::path(GlobalConfig::GetLogPath());
        std::cout << "SSH log directory: " << log_dir << std::endl;
        std::filesystem::create_directories(log_dir);
        std::cout << "Log directory created or exists" << std::endl;

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
        std::cout << "SSH log file path: " << log_path << std::endl;
        ssh_log_file.open(log_path, std::ios::out | std::ios::app);

        if (ssh_log_file.is_open()) {
            ssh_log_initialized = true;
            ssh_log_file << "=== SSH Log Session Started ===" << std::endl;
            ssh_log_file.flush();
            std::cout << "SSH log file opened successfully" << std::endl;
        } else {
            std::cerr << "Failed to open SSH log file: " << log_path << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "SSH log initialization failed: " << e.what() << std::endl;
    }
}
